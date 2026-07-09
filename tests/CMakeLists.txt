add_executable(HowlTests
    ArrangementNodeTests.cpp
    ArrangementTests.cpp
    AudioClipCommandTests.cpp
    AudioClipTests.cpp
    AudioClipWarpTests.cpp
    AudioTrackRendererTests.cpp
    AudioTrackTests.cpp
    AutomationLaneTests.cpp
    AutomationPlaybackTests.cpp
    ChannelStripTests.cpp
    ClapAdapterTests.cpp
    ClipEditCommandTests.cpp
    CommandStackTests.cpp
    CompressorTests.cpp
    DelayTests.cpp
    EffectChainTests.cpp
    EffectFactoryTests.cpp
    EffectParameterTests.cpp
    EqualizerTests.cpp
    GoldenRenderTests.cpp
    GraphTests.cpp
    LimiterTests.cpp
    LiveMidiInputTests.cpp
    MeterTests.cpp
    MidiClipTests.cpp
    MidiTrackRendererTests.cpp
    MixerBypassTests.cpp
    MixerCommandTests.cpp
    MixerPdcTests.cpp
    MixerTests.cpp
    NoteCommandTests.cpp
    OfflineStretcherTests.cpp
    PatternPlaybackTests.cpp
    PatternTests.cpp
    PluginEffectTests.cpp
    PluginHostTests.cpp
    PluginInstrumentTests.cpp
    PreviewPlayerTests.cpp
    ProjectSerializerTests.cpp
    ReverbTests.cpp
    SamplerInstrumentTests.cpp
    SandboxProtocolTests.cpp
    SequencerNodeTests.cpp
    SessionPlaybackTests.cpp
    SessionTests.cpp
    SnapGridTests.cpp
    SubtractiveSynthTests.cpp
    TrackCommandTests.cpp
    TrackFreezerTests.cpp
    TransportTests.cpp
    Vst3AdapterTests.cpp
    Vst3StateTests.cpp
)

target_compile_features(HowlTests PRIVATE cxx_std_20)

target_link_libraries(HowlTests PRIVATE
    HowlDsp
    HowlEngine
    HowlModel
    HowlPlugins
    HowlProject
    Catch2::Catch2WithMain
)

# So SandboxProtocolTests can spawn the exact helper binary this build just produced
target_compile_definitions(HowlTests PRIVATE HOWL_HOST_BINARY="$<TARGET_FILE:HowlHost>")
add_dependencies(HowlTests HowlHost)

include(Catch)
catch_discover_tests(HowlTests)
