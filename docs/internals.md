# Low level internals

This page tells you about the mechanisms inside the app: time, memory, thread safety, and the special methods of each subsystem.

## Time and units

The app has two clocks. Most DAW defects occur at the border between the two clocks.

- **Samples.** The audio thread counts time in `SampleCount` values. This is a 64 bit integer. The transport position, the loop region, and each render call use samples.
- **Ticks.** The document counts musical time in ticks. One quarter note is 960 ticks (`kTicksPerQuarter` in `src/model/Note.h`). Notes, clips, and automation points keep tick positions. Thus they keep their musical meaning when the tempo changes.

The conversion occurs at render time:

```
samplesPerTick = (60 / tempo) * sampleRate / 960
```

The snap functions are pure functions in `src/model/SnapGrid.h`. They use one global division: bar, beat, half beat, step, or off. All editors share this division through one combo box in the transport bar.

## The audio block

```cpp
struct AudioBlock {
    float** channels;
    ChannelCount numChannels;
    int numFrames;
};
```

A block does not own memory. It points to buffers that a different owner allocated. Thus the audio thread can pass a block and cut a block at no cost.

## Real time safety

Functions with the mark `[RT]` in their comments run on the audio thread. The rules on this path are absolute: no allocation, no locks, no file access, and no exceptions. The code obeys these rules in three ways.

1. **Allocate in prepare, not in process.** `ArrangementNode::prepare` allocates one scratch buffer for each track. The buffer size is the largest block that the device can supply. `process` only writes into these buffers. The mixer allocates the rings for delay compensation in the same way.
2. **Atomic values for shared scalars.** `Transport` keeps the play state, the tempo, and the position in separate atomic values. The loop region is a full structure. Thus the transport publishes it as an atomic pointer to an object that does not change. The audio thread does one atomic load and reads one stable snapshot.
3. **Queues for messages.** The UI does not touch the players when it starts a session clip. A MIDI keyboard does not touch the players when it plays a note. The requests go into `LockFreeQueue` objects with a fixed capacity of 64 slots. `ArrangementNode::process` empties the queues at the start of each block.

## The steps in ArrangementNode::process

For each track, one of three sources fills the scratch buffer:

1. **Frozen audio.** If the track has a frozen render, the node copies the samples from it. The node does not do a live render.
2. **A session player.** If the session grid started a clip on this track, the `SessionTrackPlayer` of the track makes the audio. The player obeys the quantize points for starts and stops.
3. **The timeline.** If the other two sources do not apply, the `MidiTrackRenderer` or the `AudioTrackRenderer` of the track makes the audio from the arrangement.

A `MidiTrackRenderer` does these steps. First, it reads the automation lanes of the track at the start tick of the block. It pushes each value into the instrument as a normalized parameter. Then it finds the notes that start or stop inside the block. It calls `noteOn` and `noteOff` at the correct offsets. Then the instrument makes the audio for its voices. The note source includes the clips of the track and the pattern placements that touch the block. The renderer finds the pattern notes through the pattern bank at render time. Thus a pattern edit changes the sound of each placement on the next block.

Live MIDI input uses the same path. The input hub puts the notes in a queue. The node empties the queue and sends the notes to the instrument of the selected track.

After all tracks are complete, the mixer runs. The sample preview player adds its audio after the master strip.

## The mixer and delay compensation

Each track has a `ChannelStrip`. A strip is an effect chain plus gain, pan, mute, and solo. Strips send their audio to buses or to the master. A send puts a copy of a track into a bus.

Plugins and some included effects report latency. Without compensation, a track with a lookahead limiter is late against the other tracks. The mixer examines the path from each track to the master. It finds the largest total latency. It delays each other track by the difference. It uses ring buffers that it allocated before, with a limit of `kMaxPdcSamples` (16384 samples for each channel). The result: all tracks stay aligned at the sample level with all effects.

The mixer calculates solo globally in each block. If one or more strips are solo, the mixer mutes each strip that is not solo and not on a solo path.

## Undo, redo, and the gesture rule

Each edit is a `Command` with `execute` and `undo`. The `CommandStack` performs the commands and keeps the list of completed commands. It moves through this list for undo and redo. The change counter of the stack increases at each perform, undo, and redo. The app records the counter at the last save. A different counter value means changes that are not saved. This one mechanism controls the autosave, the quit guard, and the open guard.

A note command does not hold a reference to its clip. It finds the clip through a `ClipAddress` (arrangement, session, or pattern) at execute time. Thus the command stays valid when the containers change their memory or their order.

Editor drags obey the gesture rule. The drag changes the clip directly, frame by frame. Thus you see the real model move. At mouse up, the full change becomes one `ReplaceNotesCommand`. The before set of the command is the values from mouse down. Its `execute` and `undo` do a presence check. They remove a note only when an exact match exists. They add a note only when the note is absent. The drag applied the after state before the command runs. Thus the first `execute` changes nothing, and `undo` puts back the before values exactly.

## MIDI input and MIDI learn

`MidiInputHub` opens each connected MIDI device and splits the traffic by destination. The notes go into a lock free queue that the audio thread empties. Thus a key that you play makes a sound on the selected track in one block. The control changes go into a second queue. A message thread timer empties this queue 30 times each second. When you arm a parameter for MIDI learn, the next control change connects to that parameter. From then on, the connected controller moves the effect parameter. The mappings are part of the saved project.

## The plugin sandbox

A plugin in a sandbox runs inside its own `howl-host` child process. Audio crosses the process boundary through a shared memory channel. The parent writes an input block and increases an atomic sequence number. Then the parent does a spin wait for the matching output sequence from the child. The wait has a fixed limit of some milliseconds. If the child misses the limit, the block passes through as silence. Playback does not stop. Control messages move as JSON lines through the pipes of the child. These messages include prepare, state save and load, parameter changes, and editor open and close. The native editor of the plugin opens as a top level window that the child owns.

A child that dies or stops sets the crashed flag of the instance. The plugin goes to bypass, and the UI shows it in red. A restart starts a new child and loads the last known state. You can set the sandbox to off in the Options menu. Then the plugins load in the app process.

## The track freeze function

`TrackFreezer::renderTrack` makes the audio for one track offline through its strip. The app stops the device first. Then the freezer gives the completed channels to `ArrangementNode::setFrozen`. From then on, playback copies from the frozen buffer. The track cost is almost zero. When you remove the freeze, the node discards the buffer, and live renders start again. Golden tests make sure that a frozen track and a live track make the same output.

## The audio warp function

Audio clips can follow the project tempo. `OfflineStretcher` uses the Rubber Band library. It makes the changed audio offline, away from the audio thread. It runs when the tempo or the warp setting of the clip changes. Playback then reads the prepared buffer. Thus the audio thread does not pay the cost of the time stretch.

## Sample previews

A click on a file in the browser plays the file. The project does not change. `PreviewPlayer` gives the audio thread a preview buffer through a queue. The player adds the audio after the master strip. When playback ends, the buffer comes back through a return queue. A message thread timer then releases the buffer. The audio thread does not allocate and does not release preview memory.

## Offline export

`OfflineRenderer::renderNodeToFile` writes a 24 bit WAV file. It records the transport state, sets the loop to off, and moves the position to zero. Then it runs the same `process` call that the audio device runs, block by block. It writes each block to the disk. When the render is complete, it puts the transport state back exactly. Export runs the same code path as playback. Thus golden tests can make sure that two exports of the same project are equal bit by bit.

## Plugin interfaces

`IPluginInstance` defines the contract that all formats obey. The contract has these parts: prepare and release away from the audio thread, a real time `process`, state as an opaque byte blob, normalized parameters, and an optional native editor window. `Vst3Adapter` implements the contract on the JUCE plugin host. `ClapAdapter` implements it on the CLAP C API through the clap helpers. `SandboxedPluginInstance` implements it on a child process, as this page tells you above. `PluginInstrument` and `PluginEffect` adapt a plugin instance to the `Instrument` and `Effect` interfaces of the engine. Thus a track cannot see a difference between a plugin and an included device.

`PluginHost` finds the installed plugins on a background thread. The scan loads unknown third party binaries and can be slow. The host writes the descriptor list to a cache on the disk. Thus the next start of the app is fast.

## Persistence

`ProjectSerializer` writes the full session as `.howl` JSON text. This includes the tempo, the tracks, the clips, the notes, the automation, the patterns and their placements, the session grid, the mixer settings, the effect chains, the instrument assignments, and the MIDI mappings. The plugin state is the opaque blob of the plugin, as base64 inside the JSON. The load function is tolerant. An unknown plugin does not stop the load of the file. Only a JSON parse error stops the load.

A small settings file is adjacent to the project files. It keeps the root folder of the browser, the recent projects list, and the audio device configuration. A timer makes an autosave of a changed project to an adjacent `.autosave` file every two minutes. When you open a project with a newer autosave, the app asks you if it must recover the autosave. A good save deletes the adjacent autosave file.
