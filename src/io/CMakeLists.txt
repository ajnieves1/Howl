add_library(HowlIo STATIC
    AudioDevice.cpp
    AudioFile.cpp
    MidiInputHub.cpp
)

target_include_directories(HowlIo PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(HowlIo PRIVATE cxx_std_20)

target_link_libraries(HowlIo PUBLIC
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_basics
    juce::juce_core
)
