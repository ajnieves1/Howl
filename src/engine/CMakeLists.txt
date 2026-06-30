add_library(HearthEngine STATIC
    AudioTrack.cpp
    Graph.cpp
    Transport.cpp
)

target_include_directories(HearthEngine PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(HearthEngine PRIVATE cxx_std_20)

find_package(Threads REQUIRED)

target_link_libraries(HearthEngine
    PUBLIC
        HearthIo
    PRIVATE
        juce::juce_recommended_warning_flags
        Threads::Threads
)
