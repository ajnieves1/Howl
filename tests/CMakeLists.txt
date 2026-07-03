add_executable(HowlTests
    ArrangementNodeTests.cpp
    ArrangementTests.cpp
    AudioClipCommandTests.cpp
    AudioClipTests.cpp
    AudioTrackRendererTests.cpp
    AudioTrackTests.cpp
    AutomationLaneTests.cpp
    ChannelStripTests.cpp
    ClapAdapterTests.cpp
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
    MeterTests.cpp
    MidiClipTests.cpp
    MidiTrackRendererTests.cpp
    MixerCommandTests.cpp
    MixerPdcTests.cpp
    MixerTests.cpp
    PluginEffectTests.cpp
    PluginHostTests.cpp
    PluginInstrumentTests.cpp
    ReverbTests.cpp
    SequencerNodeTests.cpp
    SubtractiveSynthTests.cpp
    TrackCommandTests.cpp
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
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(HowlTests)
