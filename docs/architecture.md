# High level architecture

Howl is a stack of static libraries. Each library is one folder in `src/`. A thin app target is at the top of the stack. A lower layer does not know the layers above it. Thus you can build and test the audio engine without the UI.

## Module stack

The modules have these direct dependencies:

- The Howl app (`src/app`) uses `HowlUi` (`src/ui`), `HowlProject` (`src/project`), and `HowlDsp` (`src/dsp`).
- `HowlUi` uses `HowlModel` (`src/model`) and `HowlPlugins` (`src/plugins`).
- `HowlProject` uses `HowlModel` and `HowlPlugins`.
- `HowlDsp`, `HowlPlugins`, and `HowlModel` use `HowlEngine` (`src/engine`).
- `HowlEngine` uses `HowlIo` (`src/io`).
- `HowlIo` uses the JUCE modules.
- `howl-host` (`src/host`) is a separate process. It speaks with `HowlPlugins` while the app operates.

## The function of each layer

**core** (headers only). This layer contains the basic types that all other layers use. These types are `SampleCount`, `AudioBlock`, and a `LockFreeQueue` with a fixed capacity. The queue moves messages from the UI thread to the audio thread.

**io** (`HowlIo`). This layer speaks with the system. `AudioDevice` controls the system audio output and starts the audio callback. `AudioFile` reads and writes WAV data through the JUCE format readers. `MidiInputHub` opens each connected MIDI device. It puts the notes in a queue for the audio thread. It puts the control changes in a queue for the message thread.

**engine** (`HowlEngine`). This layer contains the abstract audio machinery. `Node` is the interface that each processor implements. `Graph` owns the nodes and runs them in topological order. `Transport` holds the play state, the tempo, the position, and the loop region in atomic values. Thus the two threads can read the transport safely. `Instrument` is the interface for each sound source. `Effect` is the interface for each processor. `EffectChain` runs a list of effects in a set order.

**dsp** (`HowlDsp`). This layer contains the concrete sound code. It has the subtractive synthesizer and the one shot sampler. It has the included effects: the equalizer, the compressor, the limiter, the delay, the reverb, and the gain. It also has an envelope follower and an offline time stretcher. The time stretcher uses the Rubber Band library to change the length of audio.

**plugins** (`HowlPlugins`). This layer holds third party plugins. `IPluginInstance` is one interface for all plugin formats. `Vst3Adapter` and `ClapAdapter` implement this interface for the two formats. `SandboxedPluginInstance` implements the same interface. It controls a plugin that a separate child process loads. `PluginInstrument` and `PluginEffect` put a plugin instance behind the usual `Instrument` and `Effect` interfaces. Thus the app operates a plugin and an included instrument in the same way. `PluginHost` finds the plugins on the system on a background thread. It writes the results to a cache on the disk.

**host** (`howl-host`). This is a small separate binary. It loads one plugin for the app. Audio moves between the two processes through shared memory. Control messages move as JSON lines through pipes. When a plugin has a crash, only this process stops. Howl continues.

**model** (`HowlModel`). This layer is the document and the playback of the document. `Arrangement` holds the tracks, the clips, and the automation lanes. `Session` holds the clip launch grid. `PatternBank` holds the patterns and the placements of the patterns on the pattern lane of the timeline. Each pattern has one MIDI clip for each track. `ArrangementNode` is the one engine node that makes the audio for the full project. It has one renderer for each track, a mixer, buffers for frozen tracks, session players, and the sample preview player. Each edit goes through a `Command` object on a `CommandStack`. Thus each change has undo and redo. The change counter of the stack also shows when a project has changes that are not saved. This counter controls the autosave function and the guard dialogs. `SnapGrid` holds the pure functions that round tick values for the global snap setting.

**project** (`HowlProject`). This layer does serialization. `ProjectSerializer` writes the full session as `.howl` JSON text and reads it back. This includes the plugin state blobs, the patterns, the automation, and the MIDI mappings.

**ui** (`HowlUi`). This layer contains the JUCE components. `MainComponent` is the one window shell. The transport bar is at the top. A file browser column is on the left when you show it. The track headers are adjacent to the center view. The center view changes between the arrange timeline, the session grid, and the channel rack. A bottom panel holds the piano roll, the mixer, or the automation editor. Each color comes from one theme header through a shared LookAndFeel.

**app**. `Main.cpp` connects all the parts. It opens the device and builds the graph. It connects the UI callbacks to the model commands. It runs the autosave timer. It keeps the recent files and the audio device settings. It owns the file dialogs for open, save, import, and export.

## Threads and processes

The app has three types of threads and one type of child process:

1. **The message thread.** All UI code, all edits, and all file dialogs run here. Model changes occur here through commands. Timers on this thread do these tasks: they empty the MIDI control change queue, they copy the mixer state to the UI, they release old preview buffers, and they write autosaves.
2. **The audio thread.** This thread calls `Graph::process` one time for each block. Code on this path does not allocate memory, does not use locks, does not touch files, and does not throw exceptions. It reads shared state through atomic values. It receives requests through lock free queues.
3. **Worker threads.** The plugin scanner runs on its own thread and writes results to a cache on the disk. Offline jobs stop the device first and then render on the message thread. These jobs include the track freeze function, the audio warp function, and the WAV export. Thus they cannot have a race with the audio callback.
4. **Sandbox child processes.** Each plugin in a sandbox has one `howl-host` process. The audio thread sends blocks to it through shared memory with a wait limit. Thus a child that stops or dies costs a fixed time before the app sets the plugin to bypass.

An `XrunWatcher` monitors the device for buffer underruns. Thus a real time safety problem shows during development and not in your work.
