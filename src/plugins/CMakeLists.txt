add_library(HowlPlugins STATIC
    ClapAdapter.cpp
    PluginEffect.cpp
    PluginHost.cpp
    PluginInstrument.cpp
    Vst3Adapter.cpp
)

target_include_directories(HowlPlugins PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(HowlPlugins PRIVATE cxx_std_20)

find_package(Threads REQUIRED)

target_link_libraries(HowlPlugins
    PUBLIC
        HowlEngine
        juce::juce_audio_processors
    PRIVATE
        clap
        clap-helpers
        juce::juce_recommended_warning_flags
        Threads::Threads
)
