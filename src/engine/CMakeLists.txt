add_library(HowlEngine STATIC
    AudioTrack.cpp
    Graph.cpp
    Transport.cpp
)

target_include_directories(HowlEngine PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(HowlEngine PRIVATE cxx_std_20)

find_package(Threads REQUIRED)

target_link_libraries(HowlEngine
    PUBLIC
        HowlIo
    PRIVATE
        juce::juce_recommended_warning_flags
        Threads::Threads
)
