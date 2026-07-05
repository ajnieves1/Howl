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
3. **A queue for messages.** When the UI wants to launch a session clip, it cannot touch the players directly. It pushes a `LaunchRequest` into a fixed capacity `LockFreeQueue` (64 slots) and `ArrangementNode::process` drains the queue at the top of every block.

## Inside ArrangementNode::process

For each track, exactly one of three sources fills the scratch buffer:

1. **Frozen audio.** If the track has a frozen render installed, samples are copied straight from it and live rendering is skipped entirely.
2. **A session player.** If the session grid has launched a clip on this track, its `SessionTrackPlayer` renders, handling quantized starts and stops.
3. **The timeline.** Otherwise the track's `MidiTrackRenderer` or `AudioTrackRenderer` renders from the arrangement.

A `MidiTrackRenderer` first evaluates the track's automation lanes at the block start tick and pushes each value into the instrument as a normalized parameter, then finds notes whose start or end falls inside the block and calls `noteOn` and `noteOff` at the right offsets, then lets the instrument render its voices.

After all tracks render, the mixer takes over.

## The mixer and delay compensation

Each track has a `ChannelStrip`: an effect chain plus gain, pan, mute, and solo. Strips route to buses or the master, and sends tap a copy of a track into a bus.

Plugins and some built in effects report latency. Without compensation, a track running a lookahead limiter would land late against its neighbors. The mixer walks every track's path to the master, finds the longest total latency, and delays every other track by the difference using preallocated ring buffers (up to `kMaxPdcSamples`, 16384 samples per channel). The result is that all tracks stay sample aligned no matter what effects they run.

Solo is computed globally each block: if any strip is soloed, every strip that is neither soloed nor on a soloed path is treated as muted.

## Undo and redo

Every edit is a `Command` with `execute` and `undo`. The `CommandStack` performs commands, keeps the done list, and pops back and forth for undo and redo.

Commands follow a store then execute convention: the constructor captures everything needed for both directions, and `execute` and `undo` are exact inverses that can run repeatedly. Drag gestures in the UI never mutate the model while the mouse moves; they draw a preview, and on mouse up they perform one command against pristine data. That keeps a whole class of double apply bugs out of the undo history.

## Track freezing

`TrackFreezer::renderTrack` renders one track offline through its strip and hands the finished channels to `ArrangementNode::setFrozen` while the device is paused. From then on playback copies from the frozen buffer and the track costs nearly nothing. Unfreezing drops the buffer and live rendering resumes. Golden tests assert a frozen track and a live track produce matching output.

## Audio warping

Audio clips can follow the project tempo. `OfflineStretcher` wraps the Rubber Band library and renders the stretched audio offline, off the audio thread, whenever the tempo or the clip's warp setting changes. Playback then reads the prestretched buffer, so the audio thread never pays the cost of time stretching.

## Offline export

`OfflineRenderer::renderNodeToFile` writes 24 bit WAV. It saves the transport state, disables the loop, seeks to zero, then runs the same `process` call the audio device would, block by block, writing each block to disk. When done it restores the transport exactly as it was. Because export runs the identical code path as playback, golden tests can assert that two exports of the same project are bit identical.

## Plugin hosting

`IPluginInstance` defines the format neutral contract: prepare and release off the audio thread, a real time `process`, state as an opaque byte blob, normalized parameters, and an optional native editor window. `Vst3Adapter` implements it on top of the JUCE plugin host, `ClapAdapter` on top of the CLAP C API through clap helpers. `PluginInstrument` and `PluginEffect` then adapt a plugin instance to the engine's own `Instrument` and `Effect` interfaces, so a track cannot tell a plugin from a built in.

`PluginHost` scans for installed plugins on a background thread, because scanning loads arbitrary third party binaries and can be slow, and caches the descriptor list to disk so the next launch is instant.

## Persistence

`ProjectSerializer` writes the whole session as `.howl` JSON text: tempo, tracks, clips, notes, automation, mixer settings, effect chains, and instrument assignments. Plugin state is saved as the plugin's own opaque blob, base64 inside the JSON. Loading is tolerant: unknown plugins fall back gracefully instead of failing the whole file, and only a JSON parse error makes loading fail outright.
