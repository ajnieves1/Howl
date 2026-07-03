add_library(HowlProject STATIC
    ProjectSerializer.cpp
)

target_include_directories(HowlProject PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(HowlProject PRIVATE cxx_std_20)

target_link_libraries(HowlProject
    PUBLIC
        HowlModel
        HowlPlugins
        juce::juce_core
    PRIVATE
        juce::juce_recommended_warning_flags
)
