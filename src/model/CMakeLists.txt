add_library(HowlModel STATIC
    Arrangement.cpp
    AudioClip.cpp
    MidiClip.cpp
    SequencerNode.cpp
)

target_include_directories(HowlModel PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(HowlModel PRIVATE cxx_std_20)

target_link_libraries(HowlModel
    PUBLIC
        HowlEngine
    PRIVATE
        juce::juce_recommended_warning_flags
)
