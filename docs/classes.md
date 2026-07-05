# Class diagrams

Four diagrams, one per area. Methods are trimmed to the ones that explain each class's job. `[RT]` marks methods that run on the audio thread.

## Engine interfaces

Everything that makes sound implements one of these. The rest of the app programs against the interface and never cares what is behind it.

```mermaid
classDiagram
    class Node {
        <<interface>>
        +process(AudioBlock, SampleCount) [RT]
    }
    class Graph {
        +addNode(unique_ptr~Node~) NodeId
        +connect(from, to)
        +prepare()
        +process(AudioBlock, SampleCount) [RT]
    }
    class Transport {
        +play() / stop()
        +setTempo(double)
        +setPosition(SampleCount)
        +setLoop(start, end, enabled)
        +advance(numFrames) [RT]
        -atomic~bool~ playing
        -atomic~double~ tempo
        -atomic~SampleCount~ position
        -atomic~LoopRegion*~ loopRegion
    }
    class Instrument {
        <<interface>>
        +prepare(sampleRate, maxBlockSize)
        +noteOn(key, velocity) [RT]
        +noteOff(key) [RT]
        +render(AudioBlock) [RT]
        +numParameters() int
        +setParameter(index, value) [RT]
    }
    class Effect {
        <<interface>>
        +prepare(sampleRate, maxBlockSize)
        +process(AudioBlock) [RT]
        +reset()
        +latencySamples() int
        +setParameter(index, value) [RT]
    }
    class EffectChain {
        +add(unique_ptr~Effect~)
        +process(AudioBlock) [RT]
        +totalLatency() int
    }

    Graph "1" o-- "many" Node : owns
    EffectChain "1" o-- "many" Effect : owns
    class SubtractiveSynth
    class Equalizer
    class Compressor
    Instrument <|.. SubtractiveSynth
    Effect <|.. Equalizer
    Effect <|.. Compressor
```

## The document model

Plain data with tick positions. No JUCE types, no engine types, which keeps it easy to test and serialize.

```mermaid
classDiagram
    class Arrangement {
        +addTrack(name, kind) size_t
        +track(index) Track
        +addMidiClipPlacement(track, placement)
        +addAudioClipPlacement(track, placement)
    }
    class Track {
        +string name
        +TrackKind kind
        +vector~MidiClipPlacement~ midiClips
        +vector~AudioClipPlacement~ audioClips
        +vector~AutomationLaneSlot~ automation
    }
    class MidiClip {
        +addNote(Note) size_t
        +replaceNoteAt(index, Note)
        +notes() vector~Note~
        +lengthTicks() int64
    }
    class Note {
        +int64 startTick
        +int64 lengthTicks
        +int key
        +float velocity
    }
    class AudioClip {
        +sample data and warp settings
    }
    class AutomationLane {
        +addPoint(AutomationPoint)
        +removePointAt(index)
        +valueAtTick(tick) float
    }
    class AutomationPoint {
        +int64 tick
        +float value
    }
    class Session {
        +addScene() size_t
        +slot(track, scene) ClipSlot
    }
    class ClipSlot {
        +SlotContent content
        +MidiClip midiClip
        +AudioClip audioClip
    }

    Arrangement "1" o-- "many" Track
    Track "1" o-- "many" MidiClip : via placements
    Track "1" o-- "many" AudioClip : via placements
    Track "1" o-- "many" AutomationLane : via slots
    MidiClip "1" o-- "many" Note
    AutomationLane "1" o-- "many" AutomationPoint
    Session "1" o-- "many" ClipSlot
```

## Playback and mixing

`ArrangementNode` is the bridge between the document above and the engine interfaces. It is the only engine node the app needs.

```mermaid
classDiagram
    class Node {
        <<interface>>
    }
    class ArrangementNode {
        +prepare(sampleRate, maxBlockSize, channels)
        +setInstrumentForTrack(track, Instrument*)
        +requestLaunch(track, scene) bool
        +setFrozen(track, channels)
        +process(AudioBlock, SampleCount) [RT]
        -LockFreeQueue~LaunchRequest~ launchQueue
        -scratch buffers, one per track
    }
    class MidiTrackRenderer {
        +process(AudioBlock, SampleCount) [RT]
    }
    class AudioTrackRenderer {
        +process(AudioBlock, SampleCount) [RT]
    }
    class SessionTrackPlayer {
        +queueScene / queueStop
        +process(AudioBlock, SampleCount) [RT]
    }
    class Mixer {
        +trackStrip(index) ChannelStrip
        +masterStrip() ChannelStrip
        +addBus(name)
        +process(track buffers, out) [RT]
        -delay compensation rings
    }
    class ChannelStrip {
        +setGainDb / setPan / setMuted / setSoloed
        +effects() EffectChain
        +process(AudioBlock) [RT]
    }

    Node <|-- ArrangementNode
    ArrangementNode "1" o-- "many" MidiTrackRenderer
    ArrangementNode "1" o-- "many" AudioTrackRenderer
    ArrangementNode "1" o-- "many" SessionTrackPlayer
    ArrangementNode "1" *-- "1" Mixer
    Mixer "1" o-- "many" ChannelStrip
    MidiTrackRenderer --> Instrument : renders through
    class Instrument {
        <<interface>>
    }
```

## Commands and plugins

Left side: every edit is a command, so the stack can walk history in both directions. Right side: both plugin formats hide behind one interface, then get wrapped to look like native instruments and effects.

```mermaid
classDiagram
    class Command {
        <<interface>>
        +execute()
        +undo()
    }
    class CommandStack {
        +perform(unique_ptr~Command~)
        +undo()
        +redo()
    }
    class AddNoteCommand
    class MoveClipCommand
    class AddAutomationPointCommand
    Command <|.. AddNoteCommand
    Command <|.. MoveClipCommand
    Command <|.. AddAutomationPointCommand
    CommandStack "1" o-- "many" Command : history

    class IPluginInstance {
        <<interface>>
        +prepare / release
        +process(AudioBlock, midi) [RT]
        +saveState() StateBlob
        +loadState(StateBlob)
        +setParamNormalized(id, value)
        +openEditor(parentHandle)
    }
    class Vst3Adapter
    class ClapAdapter
    class PluginInstrument
    class PluginEffect
    class PluginHost {
        +rescan()
        +list() vector~PluginDescriptor~
        +instantiate(descriptor) unique_ptr~IPluginInstance~
    }
    IPluginInstance <|.. Vst3Adapter
    IPluginInstance <|.. ClapAdapter
    PluginInstrument --> IPluginInstance : wraps as Instrument
    PluginEffect --> IPluginInstance : wraps as Effect
    PluginHost ..> IPluginInstance : creates
```
