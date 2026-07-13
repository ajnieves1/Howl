# Low level internals

This page covers the mechanisms under the hood: time, memory, thread safety, and the specific tricks each subsystem uses.

## Time and units

Two clocks exist and every bug in a DAW lives at the border between them.

- **Samples.** The audio thread counts time in `SampleCount` (a 64 bit integer). The transport position, the loop region, and every render call use samples.
- **Ticks.** The document counts musical time in ticks, with 960 ticks per quarter note (`kTicksPerQuarter` in `src/model/Note.h`). Notes, clips, and automation points store tick positions so they keep their musical meaning when the tempo changes.

Conversion happens at render time:

```
samplesPerTick = (60 / tempo) * sampleRate / 960
```

Snapping is a set of pure functions in `src/model/SnapGrid.h` over a global division (bar, beat, half beat, step, or off) that every editor shares through one combo box in the transport bar.

## The audio block

```cpp
struct AudioBlock {
    float** channels;
    ChannelCount numChannels;
    int numFrames;
};
```

A block never owns memory. It points at buffers someone else allocated, which makes it free to pass around and slice on the audio thread.

## Real time safety

Functions marked `[RT]` in comments run on the audio thread. The rules on that path are absolute: no allocation, no locks, no file access, no exceptions. The code keeps them three ways.

1. **Allocate in prepare, never in process.** `ArrangementNode::prepare` allocates one scratch buffer per track, sized for the largest block the device will deliver. `process` only writes into them. The mixer preallocates its delay compensation rings the same way.
2. **Atomics for shared scalars.** `Transport` stores playing, tempo, and position in individual atomics. The loop region is a whole struct, so the transport publishes it as an atomic pointer to an immutable object; the audio thread does one atomic load and reads a consistent snapshot.
3. **Queues for messages.** When the UI wants to launch a session clip, or a MIDI keyboard plays a note, nothing touches the players directly. Requests go into fixed capacity `LockFreeQueue`s (64 slots) and `ArrangementNode::process` drains them at the top of every block.

## Inside ArrangementNode::process

For each track, exactly one of three sources fills the scratch buffer:

1. **Frozen audio.** If the track has a frozen render installed, samples are copied straight from it and live rendering is skipped entirely.
2. **A session player.** If the session grid has launched a clip on this track, its `SessionTrackPlayer` renders, handling quantized starts and stops.
3. **The timeline.** Otherwise the track's `MidiTrackRenderer` or `AudioTrackRenderer` renders from the arrangement.

A `MidiTrackRenderer` first evaluates the track's automation lanes at the block start tick and pushes each value into the instrument as a normalized parameter, then finds notes whose start or end falls inside the block and calls `noteOn` and `noteOff` at the right offsets, then lets the instrument render its voices. Its note source covers both the track's own clips and any pattern placements overlapping the block, resolved through the pattern bank at render time; that resolution is what makes patterns live linked, since editing a pattern edits what every placement of it plays on the next block.

Live MIDI input rides the same path: the input hub queues incoming notes, the node drains them and routes them to the selected track's instrument.

After all tracks render, the mixer takes over, and the sample preview player mixes in after the master strip.

## The mixer and delay compensation

Each track has a `ChannelStrip`: an effect chain plus gain, pan, mute, and solo. Strips route to buses or the master, and sends tap a copy of a track into a bus.

Plugins and some built in effects report latency. Without compensation, a track running a lookahead limiter would land late against its neighbors. The mixer walks every track's path to the master, finds the longest total latency, and delays every other track by the difference using preallocated ring buffers (up to `kMaxPdcSamples`, 16384 samples per channel). The result is that all tracks stay sample aligned no matter what effects they run.

Solo is computed globally each block: if any strip is soloed, every strip that is neither soloed nor on a soloed path is treated as muted.

## Undo, redo, and the gesture rule

Every edit is a `Command` with `execute` and `undo`. The `CommandStack` performs commands, keeps the done list, and pops back and forth for undo and redo. Its change counter increments on every perform, undo, and redo; the app remembers the counter at the last save, and any difference means unsaved changes, which is the single mechanism behind autosave, the quit guard, and the open guard.

Note commands resolve their clip through a `ClipAddress` (arrangement, session, or pattern) at execute time instead of holding a reference, so a command stays valid even after the containers it points into reallocate or re-sort.

Editor drags follow the gesture rule: the drag mutates the resolved clip directly, frame by frame, so the user watches the real model move; on mouse up the whole net change becomes one `ReplaceNotesCommand` whose before set is the values captured at mouse down. Its `execute` and `undo` are presence checked, removing a note only when an exact match exists and adding one only when absent, so performing the command after the drag already applied the after state is a harmless no op rather than a double apply, and undo still restores the before values exactly.

## MIDI input and MIDI learn

`MidiInputHub` opens every connected MIDI device and splits traffic by destination. Notes go into a lock free queue the audio thread drains, so playing a key sounds on the selected track within a block. Control changes go into a second queue drained by a 30 Hz message thread timer; when a parameter is armed for MIDI learn, the next control change binds to it, and bound mappings apply controller movements to effect parameters from then on. Mappings save with the project.

## The plugin sandbox

A sandboxed plugin runs inside its own `howl-host` child process. Audio crosses the process boundary through a shared memory channel: the parent writes an input block, bumps an atomic sequence number, and spin waits under a fixed deadline of a couple of milliseconds for the child's matching output sequence. If the child misses the deadline, the block passes through silent and playback never stalls. Control messages (prepare, state save and load, parameter changes, editor open and close) travel as JSON lines over the child's pipes, and the plugin's native editor opens as a top level window owned by the child.

A dead or hung child flips the instance's crashed flag: the plugin bypasses, the UI marks it in red, and a restart respawns the child and restores the last known state. Sandboxing can be turned off in the Options menu, which loads plugins in process instead.

## Track freezing

`TrackFreezer::renderTrack` renders one track offline through its strip and hands the finished channels to `ArrangementNode::setFrozen` while the device is paused. From then on playback copies from the frozen buffer and the track costs nearly nothing. Unfreezing drops the buffer and live rendering resumes. Golden tests assert a frozen track and a live track produce matching output.

## Audio warping

Audio clips can follow the project tempo. `OfflineStretcher` wraps the Rubber Band library and renders the stretched audio offline, off the audio thread, whenever the tempo or the clip's warp setting changes. Playback then reads the prestretched buffer, so the audio thread never pays the cost of time stretching.

## Sample previews

Clicking a file in the browser plays it without touching the project. `PreviewPlayer` hands the audio thread a preview buffer through a queue, mixes it in after the master strip, and gets the buffer back through a return queue once playback ends, where a message thread timer frees it. The audio thread never allocates or frees preview memory.

## Offline export

`OfflineRenderer::renderNodeToFile` writes 24 bit WAV. It saves the transport state, disables the loop, seeks to zero, then runs the same `process` call the audio device would, block by block, writing each block to disk. When done it restores the transport exactly as it was. Because export runs the identical code path as playback, golden tests can assert that two exports of the same project are bit identical.

## Plugin hosting

`IPluginInstance` defines the format neutral contract: prepare and release off the audio thread, a real time `process`, state as an opaque byte blob, normalized parameters, and an optional native editor window. `Vst3Adapter` implements it on top of the JUCE plugin host, `ClapAdapter` on top of the CLAP C API through clap helpers, and `SandboxedPluginInstance` on top of a child process as described above. `PluginInstrument` and `PluginEffect` then adapt a plugin instance to the engine's own `Instrument` and `Effect` interfaces, so a track cannot tell a plugin from a built in.

`PluginHost` scans for installed plugins on a background thread, because scanning loads arbitrary third party binaries and can be slow, and caches the descriptor list to disk so the next launch is instant.

## Persistence

`ProjectSerializer` writes the whole session as `.howl` JSON text: tempo, tracks, clips, notes, automation, patterns and their placements, the session grid, mixer settings, effect chains, instrument assignments, and MIDI mappings. Plugin state is saved as the plugin's own opaque blob, base64 inside the JSON. Loading is tolerant: unknown plugins fall back gracefully instead of failing the whole file, and only a JSON parse error makes loading fail outright.

Alongside the project files, a small settings file remembers the browser's root folder, the recent projects list, and the audio device configuration. While you work, a timer autosaves any dirty project with a path to a sibling `.autosave` file every two minutes; opening a project whose autosave is newer offers to recover it, and a successful save deletes the sibling.
