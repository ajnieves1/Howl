add_library(HearthEngine STATIC
    Graph.cpp
    Transport.cpp
)

target_include_directories(HearthEngine PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(HearthEngine PRIVATE cxx_std_20)

target_link_libraries(HearthEngine PRIVATE
    juce::juce_recommended_warning_flags
)
