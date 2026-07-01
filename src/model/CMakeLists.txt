add_library(HearthModel STATIC
    Arrangement.cpp
    AudioClip.cpp
    MidiClip.cpp
    SequencerNode.cpp
)

target_include_directories(HearthModel PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(HearthModel PRIVATE cxx_std_20)

target_link_libraries(HearthModel
    PUBLIC
        HearthEngine
    PRIVATE
        juce::juce_recommended_warning_flags
)
