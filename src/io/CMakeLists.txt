add_library(HearthIo STATIC
    AudioDevice.cpp
)

target_include_directories(HearthIo PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(HearthIo PRIVATE cxx_std_20)

target_link_libraries(HearthIo PUBLIC
    juce::juce_audio_devices
    juce::juce_audio_basics
    juce::juce_core
)
