add_library(HearthPlugins STATIC
    ClapAdapter.cpp
    PluginHost.cpp
    Vst3Adapter.cpp
)

target_include_directories(HearthPlugins PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(HearthPlugins PRIVATE cxx_std_20)

find_package(Threads REQUIRED)

target_link_libraries(HearthPlugins
    PUBLIC
        juce::juce_audio_processors
    PRIVATE
        clap
        clap-helpers
        juce::juce_recommended_warning_flags
        Threads::Threads
)
