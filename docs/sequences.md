# Operation sequences

This page tells you about six flows. Each flow shows how the parts speak with each other. The important steps are the steps that cross a thread boundary or a process boundary.

## 1. The render of one audio block

The device calls the app on the audio thread one time for each block. All steps after the first step are real time code.

1. The `AudioDevice` calls `Graph::process(block, pos)`.
2. The `Graph` calls `ArrangementNode::process(block, pos)`.
3. The node empties the launch queue and the live note queue.
4. The node fills the scratch buffer of each track from one source:
    - If the track is frozen, the node copies the samples from the frozen buffer.
    - If a session clip plays on the track, the `SessionTrackPlayer` makes the audio.
    - If the two conditions above do not apply, the track renderer makes the audio from the timeline.
5. For a MIDI track, the `MidiTrackRenderer` does these steps:
    1. It sends the automation values to the instrument with `setParameter`.
    2. It calls `noteOn` and `noteOff` at the correct block offsets. The notes come from the clips and from the pattern placements.
    3. It calls `render(scratch)` on the instrument.
6. The node calls `Mixer::process(track buffers, out)`.
7. The mixer runs the effects, the delay compensation, the gain, and the pan of each strip.
8. The mixer sums the buses and the master, and runs the master chain.
9. The node adds the sample preview audio after the master.
10. The device receives the mixed block.

## 2. A note edit with undo

All steps run on the message thread. The drag changes the clip live. Thus you see the real model move. At mouse up, the full change becomes one command. The `execute` of this command changes nothing at that moment. The `undo` of this command puts back the values from mouse down.

1. You drag a note in the `PianoRoll`.
2. At each mouse move, the piano roll changes the `MidiClip` directly. The roll finds the clip through a `ClipAddress`.
3. You release the mouse.
4. The piano roll calls `CommandStack::perform` with a `ReplaceNotesCommand`. The command holds the before set and the after set.
5. The stack calls `execute()` on the command.
6. The command does a presence check on the clip. The drag applied the after state before. Thus the command changes nothing.
7. You push Ctrl+Z.
8. The piano roll calls `CommandStack::undo()`.
9. The stack calls `undo()` on the command.
10. The command puts the before set back into the clip.

## 3. The start of a session clip

The click occurs on the message thread. The start occurs on the audio thread. A lock free queue carries the request across the thread boundary.

1. You click a clip slot in the `SessionView`.
2. The view calls `ArrangementNode::requestLaunch(track, scene)`.
3. The node pushes a `LaunchRequest` into the `LockFreeQueue`. This queue is the thread boundary.
4. At the start of the next `process` call, the node takes the request from the queue.
5. The node sends the scene to the `SessionTrackPlayer`.
6. The player waits for the next quantize point.
7. The player starts the clip.
8. A UI timer polls `activeScene(track)` on the node.
9. The view paints the slot as active.

## 4. A plugin crash in the sandbox and the restart

The plugin lives in its own process. The wait of the audio thread has a limit. Thus a dead child costs one deadline and cannot stop the app.

1. The audio thread calls `process(block)` on the `SandboxedPluginInstance`.
2. The instance writes the input to the shared memory channel and increases the sequence number. The channel is the process boundary.
3. The `howl-host` child reads the input, and the plugin makes the audio.
4. The plugin has a crash. The child process dies.
5. The instance waits for the output sequence.
6. The wait misses the deadline.
7. The instance sends silence and sets the crashed flag.
8. From then on, the instance passes the audio through without a change.
9. You select Restart Plugin from the menu of the red row.
10. The instance starts a new child.
11. The instance prepares the child and loads the last known state.
12. The instance and the child send blocks again.

## 5. The export of a WAV file

The export stops the device. Then it uses the same code path as playback, offline. Thus the file is equal to the sound that you hear.

1. You select File, then Export Audio.
2. The menu calls `onExportAudioRequested` on the app.
3. The app shows a save dialog for `*.wav`.
4. You select the destination.
5. The app calls `stop()` on the `AudioDevice`.
6. The app calls `resetSessionPlayback()` on the `ArrangementNode`.
7. The app calls `OfflineRenderer::renderNodeToFile(node, transport, file)`.
8. The renderer records the transport state, sets the loop to off, and moves the position to zero.
9. Until the end of the project, the renderer calls `process(block, pos)` on the node and writes each block as 24 bit WAV.
10. The renderer puts the transport state back.
11. The app calls `start()` on the device.

## 6. The save of a project

1. You select File, then Save, or you push Ctrl+S.
2. The menu calls `onSaveProjectRequested` on the app.
3. The app calls `serialize` on the `ProjectSerializer`. The call includes the arrangement, the mixer, the session, the patterns, the instruments, the tempo, and the MIDI mappings.
4. The serializer calls `saveState()` on each `IPluginInstance`.
5. Each plugin returns its opaque state blob.
6. The serializer returns the `.howl` JSON text to the app.
7. The app writes the file, records the change counter, and deletes the adjacent autosave file.
