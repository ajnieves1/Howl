add_library(HowlModel STATIC
    Arrangement.cpp
    ArrangementNode.cpp
    AudioClip.cpp
    AudioTrackRenderer.cpp
    AutomationLane.cpp
    ChannelStrip.cpp
    CommandStack.cpp
    Commands.cpp
    MidiClip.cpp
    MidiTrackRenderer.cpp
    Mixer.cpp
    OfflineRenderer.cpp
    SequencerNode.cpp
)

target_include_directories(HowlModel PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(HowlModel PRIVATE cxx_std_20)

target_link_libraries(HowlModel
    PUBLIC
        HowlEngine
        HowlIo
    PRIVATE
        juce::juce_recommended_warning_flags
)
