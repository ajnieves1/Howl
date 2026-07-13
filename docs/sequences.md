# Sequence diagrams

Six flows that show how the pieces talk to each other. Arrows crossing from the UI column to the engine columns are the interesting ones, because that is where thread and process boundaries live.

## 1. Rendering one audio block

The device calls back on the audio thread once per block. Everything below the device line is real time code.

```mermaid
sequenceDiagram
    participant Device as AudioDevice (audio thread)
    participant Graph
    participant Node as ArrangementNode
    participant Renderer as MidiTrackRenderer
    participant Synth as Instrument
    participant Mixer

    Device->>Graph: process(block, pos)
    Graph->>Node: process(block, pos)
    Node->>Node: drain launch and live note queues
    loop each track
        alt track is frozen
            Node->>Node: copy from frozen buffer
        else session clip playing
            Node->>Node: SessionTrackPlayer renders
        else timeline playback
            Node->>Renderer: process(scratch, pos)
            Renderer->>Synth: setParameter(automation values)
            Renderer->>Synth: noteOn / noteOff at block offsets (clips and pattern placements)
            Renderer->>Synth: render(scratch)
        end
    end
    Node->>Mixer: process(track buffers, out)
    Mixer->>Mixer: per strip effects, delay compensation, gain, pan
    Mixer->>Mixer: sum buses and master, run master chain
    Node->>Node: mix sample preview after the master
    Mixer-->>Device: mixed block
```

## 2. Editing a note with undo

All on the message thread. The drag mutates the clip live so the user watches the real model move; mouse up turns the net change into one command whose execute is a harmless no op at that moment, and whose undo restores the values captured at mouse down.

```mermaid
sequenceDiagram
    actor User
    participant Roll as PianoRoll
    participant Clip as MidiClip (via ClipAddress)
    participant Stack as CommandStack
    participant Cmd as ReplaceNotesCommand

    User->>Roll: drag a note
    loop every mouse move
        Roll->>Clip: mutate directly (live feedback)
    end
    User->>Roll: release mouse
    Roll->>Stack: perform(ReplaceNotesCommand(before set, after set))
    Stack->>Cmd: execute()
    Cmd->>Clip: presence checked swap (already applied, no op)

    User->>Roll: press Ctrl+Z
    Roll->>Stack: undo()
    Stack->>Cmd: undo()
    Cmd->>Clip: restore the before set
```

## 3. Launching a session clip

The click happens on the message thread, the launch happens on the audio thread, and a lock free queue carries the request across.

```mermaid
sequenceDiagram
    actor User
    participant View as SessionView (message thread)
    participant Queue as LockFreeQueue
    participant Node as ArrangementNode (audio thread)
    participant Player as SessionTrackPlayer

    User->>View: click a clip slot
    View->>Node: requestLaunch(track, scene)
    Node->>Queue: push LaunchRequest
    Note over Queue: thread boundary

    Node->>Queue: pop (top of next process call)
    Node->>Player: queue scene
    Player->>Player: wait for the next quantize point
    Player->>Player: start playing the clip
    View->>Node: activeScene(track) polled by a UI timer
    View->>View: repaint slot as playing
```

## 4. A sandboxed plugin crashing and restarting

The plugin lives in its own process. The audio thread's wait on it is bounded, so a dead child costs one deadline, never a hang.

```mermaid
sequenceDiagram
    participant Node as Audio thread
    participant Sandbox as SandboxedPluginInstance
    participant Shm as Shared memory channel
    participant Child as howl-host (child process)
    actor User

    Node->>Sandbox: process(block)
    Sandbox->>Shm: write input, bump sequence
    Note over Shm: process boundary
    Shm->>Child: child reads, plugin renders
    Child--xChild: plugin crashes, process dies
    Sandbox->>Shm: wait for output sequence
    Shm-->>Sandbox: deadline missed
    Sandbox->>Sandbox: output silence, flip crashed flag
    Sandbox-->>Node: passthrough from now on

    User->>Sandbox: Restart Plugin (from the red row's menu)
    Sandbox->>Child: spawn a new child
    Sandbox->>Child: prepare, restore last known state
    Sandbox-->>Node: exchanging blocks again
```

## 5. Exporting a WAV file

Export pauses the device, then reuses the exact playback code path offline, so what you hear is what you get.

```mermaid
sequenceDiagram
    actor User
    participant Menu as MainComponent menu
    participant App as Main (app)
    participant Device as AudioDevice
    participant Renderer as OfflineRenderer
    participant Node as ArrangementNode

    User->>Menu: File, Export Audio
    Menu->>App: onExportAudioRequested
    App->>User: save dialog (*.wav)
    User->>App: choose destination
    App->>Device: stop()
    App->>Node: resetSessionPlayback()
    App->>Renderer: renderNodeToFile(node, transport, file)
    Renderer->>Renderer: save transport state, disable loop, seek to 0
    loop until project end
        Renderer->>Node: process(block, pos)
        Renderer->>Renderer: write block as 24 bit WAV
    end
    Renderer->>Renderer: restore transport state
    App->>Device: start()
```

## 6. Saving a project

```mermaid
sequenceDiagram
    actor User
    participant Menu as MainComponent menu
    participant App as Main (app)
    participant Ser as ProjectSerializer
    participant Plugin as IPluginInstance

    User->>Menu: File, Save (or Ctrl+S)
    Menu->>App: onSaveProjectRequested
    App->>Ser: serialize(arrangement, mixer, session, patterns, instruments, tempo, midi mappings)
    Ser->>Plugin: saveState() for each hosted plugin
    Plugin-->>Ser: opaque state blob
    Ser-->>App: .howl JSON text
    App->>App: write file, record the change counter, delete the sibling autosave
```
