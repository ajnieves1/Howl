add_library(HearthPlugins STATIC
    PluginHost.cpp
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
        juce::juce_recommended_warning_flags
        Threads::Threads
)
