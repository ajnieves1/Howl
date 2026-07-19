# Class reference

This page tells you about the important classes in four areas. It lists only the methods that show the function of each class. The mark `[RT]` identifies a method that runs on the audio thread.

## Engine interfaces

Each part that makes sound implements one of these interfaces. The rest of the app uses the interface and does not know the implementation.

**`Node`** is an interface. Each processor in the graph implements it.
- `process(AudioBlock, SampleCount)` `[RT]`

**`Graph`** owns many `Node` objects and runs them in topological order.
- `addNode(unique_ptr<Node>)` returns a `NodeId`
- `connect(from, to)`
- `prepare()`
- `process(AudioBlock, SampleCount)` `[RT]`

**`Transport`** holds the play state, the tempo, the position, and the loop region. It keeps these values in atomic members: `playing`, `tempo`, `position`, and a pointer named `loopRegion`. Thus the two threads can read them safely.
- `play()` and `stop()`
- `setTempo(double)`
- `setPosition(SampleCount)`
- `setLoop(start, end, enabled)`
- `advance(numFrames)` `[RT]`

**`Instrument`** is an interface. Each sound source implements it.
- `prepare(sampleRate, maxBlockSize)`
- `noteOn(key, velocity)` `[RT]`
- `noteOff(key)` `[RT]`
- `render(AudioBlock)` `[RT]`
- `numParameters()` returns an `int`
- `setParameter(index, value)` `[RT]`

**`Effect`** is an interface. Each audio processor implements it.
- `prepare(sampleRate, maxBlockSize)`
- `process(AudioBlock)` `[RT]`
- `reset()`
- `latencySamples()` returns an `int`
- `setParameter(index, value)` `[RT]`

**`EffectChain`** owns many `Effect` objects and runs them in a set order.
- `add(unique_ptr<Effect>)`
- `process(AudioBlock)` `[RT]`
- `totalLatency()` returns an `int`

These relations connect the classes:

- A `Graph` owns many `Node` objects.
- An `EffectChain` owns many `Effect` objects.
- `SubtractiveSynth` and `SamplerInstrument` implement `Instrument`.
- `Equalizer` and `Compressor` implement `Effect`.

## The document model

The document model is plain data with tick positions. It has no JUCE types and no engine types. Thus the model is easy to test and to serialize.

**`Arrangement`** holds the tracks.
- `addTrack(name, kind)` returns a `size_t`
- `track(index)` returns a `Track`
- `addMidiClipPlacement(track, placement)`
- `addAudioClipPlacement(track, placement)`

**`Track`** holds the data of one track.
- `string name`
- `TrackKind kind`
- `vector<MidiClipPlacement> midiClips`
- `vector<AudioClipPlacement> audioClips`
- `vector<AutomationLaneSlot> automation`

**`MidiClip`** holds the notes of one clip.
- `addNote(Note)` returns a `size_t`
- `replaceNoteAt(index, Note)`
- `notes()` returns a `vector<Note>`
- `lengthTicks()` returns an `int64`

**`Note`** is one note.
- `int64 startTick`
- `int64 lengthTicks`
- `int key`
- `float velocity`

**`AudioClip`** holds the sample data and the warp settings of one audio clip.

**`AutomationLane`** holds the points of one automation lane.
- `addPoint(AutomationPoint)`
- `removePointAt(index)`
- `valueAtTick(tick)` returns a `float`

**`Session`** holds the clip launch grid.
- `addScene()` returns a `size_t`
- `slot(track, scene)` returns a `ClipSlot`

**`ClipSlot`** is one cell in the grid.
- `SlotContent content`
- `MidiClip midiClip`
- `AudioClip audioClip`

**`PatternBank`** holds the patterns and their placements.
- `addPattern(name, numTracks)` returns a `size_t`
- `pattern(index)` returns a `Pattern`
- `placements()` returns a `vector<PatternPlacement>`

**`Pattern`** is one pattern. It has a `string name` and a `vector<MidiClip> trackClips`, with one clip for each track.

**`PatternPlacement`** puts a pattern on the timeline. It has a `size_t patternIndex` and an `int64 startTick`.

**`ClipAddress`** identifies one clip. Its `Kind` is `Arrangement`, `Session`, or `Pattern`, with the indices of the clip.

These relations connect the classes:

- An `Arrangement` owns many `Track` objects.
- A `Track` refers to many `MidiClip` and `AudioClip` objects through placements.
- A `Track` owns many `AutomationLane` objects through slots.
- A `MidiClip` owns many `Note` objects.
- A `Session` owns many `ClipSlot` objects.
- A `PatternBank` owns many `Pattern` and `PatternPlacement` objects.
- A `Pattern` owns one `MidiClip` for each track.
- A `ClipAddress` points to one `MidiClip`.

## Playback and the mixer

`ArrangementNode` is the bridge between the document model and the engine interfaces. It is the only engine node that the app needs. It implements `Node`.

**`ArrangementNode`** makes the audio for the full project. It owns a `LockFreeQueue<LaunchRequest>` and one scratch buffer for each track.
- `prepare(sampleRate, maxBlockSize, channels)`
- `setInstrumentForTrack(track, Instrument*)`
- `setPatternBank(PatternBank*)`
- `setLiveNoteQueue(queue*)`
- `setPreviewPlayer(PreviewPlayer*)`
- `requestLaunch(track, scene)` returns a `bool`
- `setFrozen(track, channels)`
- `process(AudioBlock, SampleCount)` `[RT]`

**`MidiTrackRenderer`** makes the audio for one MIDI track through its instrument.
- `setPatternSource(bank, trackIndex)`
- `process(AudioBlock, SampleCount)` `[RT]`

**`AudioTrackRenderer`** makes the audio for one audio track.
- `process(AudioBlock, SampleCount)` `[RT]`

**`SessionTrackPlayer`** plays session clips on one track.
- `queueScene` and `queueStop`
- `process(AudioBlock, SampleCount)` `[RT]`

**`PreviewPlayer`** plays sample previews after the master strip.
- `post(unique_ptr<PreviewBuffer>)`
- `stop()`
- `process(AudioBlock)` `[RT]`
- `collectGarbage()`

**`Mixer`** mixes the track buffers into the output. It owns the rings for delay compensation.
- `trackStrip(index)` returns a `ChannelStrip`
- `masterStrip()` returns a `ChannelStrip`
- `addBus(name)`
- `process(track buffers, out)` `[RT]`

**`ChannelStrip`** is the strip of one track, one bus, or the master.
- `setGainDb`, `setPan`, `setMuted`, and `setSoloed`
- `effects()` returns an `EffectChain`
- `process(AudioBlock)` `[RT]`

These relations connect the classes:

- `ArrangementNode` implements `Node`.
- An `ArrangementNode` owns many `MidiTrackRenderer`, `AudioTrackRenderer`, and `SessionTrackPlayer` objects.
- An `ArrangementNode` owns one `Mixer`.
- An `ArrangementNode` adds the `PreviewPlayer` audio after the master.
- A `Mixer` owns many `ChannelStrip` objects.
- A `MidiTrackRenderer` makes its audio through an `Instrument`.

## Commands and plugins

Each edit is a command. Thus the stack can move through the history in the two directions. The two plugin formats are behind one interface. Wrappers then make a plugin equal to a native instrument or effect. The sandboxed instance is a third implementation that puts a full process behind the same interface.

**`Command`** is an interface with `execute()` and `undo()`.

**`CommandStack`** performs the commands and keeps the history.
- `perform(unique_ptr<Command>)`
- `undo()`
- `redo()`
- `changeCounter()` returns a `uint64`

**`AddNoteCommand`**, **`ReplaceNotesCommand`**, and **`MoveMidiClipCommand`** implement `Command`. `AddNoteCommand` and `ReplaceNotesCommand` find their clip through a `ClipAddress` at execute time.

**`CompositeCommand`** implements `Command` and owns many child commands.
- `add(unique_ptr<Command>)`

**`IPluginInstance`** is the interface for all plugin formats.
- `prepare` and `release`
- `process(AudioBlock, midi)` `[RT]`
- `saveState()` returns a `StateBlob`
- `loadState(StateBlob)`
- `setParamNormalized(id, value)`
- `openEditor(parentHandle)`

**`Vst3Adapter`** and **`ClapAdapter`** implement `IPluginInstance` for their formats.

**`SandboxedPluginInstance`** implements `IPluginInstance` with a `howl-host` child process. The audio moves through shared memory. The control messages move as JSON lines.
- `hasCrashed()` returns a `bool`
- `restart()`

**`PluginInstrument`** and **`PluginEffect`** put an `IPluginInstance` behind the `Instrument` and `Effect` interfaces.

**`PluginHost`** finds the plugins and makes the instances.
- `rescan()`
- `list()` returns a `vector<PluginDescriptor>`
- `setSandboxed(bool)`
- `instantiate(descriptor)` returns a `unique_ptr<IPluginInstance>`

These relations connect the classes:

- A `CommandStack` owns many `Command` objects as its history.
- A `CompositeCommand` owns many `Command` objects as its children.
- A `SandboxedPluginInstance` controls one `howl-host` child process.
- A `PluginHost` makes `IPluginInstance` objects.
