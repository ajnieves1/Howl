add_library(HearthModel STATIC
    MidiClip.cpp
)

target_include_directories(HearthModel PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(HearthModel PRIVATE cxx_std_20)

target_link_libraries(HearthModel PRIVATE
    juce::juce_recommended_warning_flags
)
