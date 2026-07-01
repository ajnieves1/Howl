add_executable(HearthTests
    ArrangementTests.cpp
    AudioClipTests.cpp
    AudioTrackTests.cpp
    ClapAdapterTests.cpp
    GraphTests.cpp
    MeterTests.cpp
    MidiClipTests.cpp
    PluginHostTests.cpp
    SequencerNodeTests.cpp
    SubtractiveSynthTests.cpp
    TransportTests.cpp
    Vst3AdapterTests.cpp
    Vst3StateTests.cpp
)

target_compile_features(HearthTests PRIVATE cxx_std_20)

target_link_libraries(HearthTests PRIVATE
    HearthDsp
    HearthEngine
    HearthModel
    HearthPlugins
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(HearthTests)
