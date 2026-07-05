# Howl

<img width="1909" height="1023" alt="image" src="https://github.com/user-attachments/assets/2b4b918e-3d8f-4f1d-9922-b781abd0f570" />


Howl is a digital audio workstation written from scratch in C++20 on the JUCE 8 framework. It covers the core of what a DAW needs, a timeline, a piano roll, a session grid, a mixer, plugin hosting, and clean offline rendering, without the bloat that piles up in commercial products, and most importantly, supports VST3.

## Features

- Arrangement timeline with MIDI and audio tracks, clip editing, zoom, a loop region, and seek
- Session view with a clip launch grid and quantized scene launching
- Piano roll note editing with full undo and redo
- Mixer with per track gain, pan, mute, solo, effect chains, sends, buses, and plugin delay compensation
- Built in effects: equalizer, compressor, limiter, delay, reverb, and gain
- Built in subtractive synthesizer
- VST3 and CLAP plugin hosting behind one shared adapter interface
- Track freezing, heavy tracks bounce to audio and play back from the frozen render
- Audio warping through the Rubber Band time stretcher
- Instrument parameter automation lanes evaluated during playback
- WAV import and 24 bit WAV export through a deterministic offline renderer
- Project save and load as plain JSON text (.howl files)

## Building

You need CMake 3.22 or newer, Ninja, and a C++20 compiler. All dependencies (JUCE 8.0.4, Catch2 Testing Framework, CLAP, Rubber Band) are fetched and pinned automatically by CMake

```
cmake -B build -G Ninja
cmake --build build
```

The app target is `Howl`. 

You are able to run the app's binary from build/src/Howl_artefacts 

## Tests

```
ctest --test-dir build
```

The suite has over 140 test cases across 43 files, covering the audio engine, the mixer, DSP, plugin adapters, serialization, and golden render checks that assert offline output is deterministic down to the sample.

## Documentation

- [High level architecture](docs/architecture.md)
- [Low level internals](docs/internals.md)
- [Sequence diagrams](docs/sequences.md)
- [Class diagrams](docs/classes.md)

## License

GPL 3.0 or later.
