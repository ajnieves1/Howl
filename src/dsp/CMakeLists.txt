add_library(HearthDsp STATIC
    SubtractiveSynth.cpp
)

target_include_directories(HearthDsp PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(HearthDsp PRIVATE cxx_std_20)

target_link_libraries(HearthDsp
    PUBLIC
        HearthEngine
    PRIVATE
        juce::juce_recommended_warning_flags
)
