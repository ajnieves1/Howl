add_library(HearthUi STATIC
    PianoRoll.cpp
    PluginWindow.cpp
)

target_include_directories(HearthUi PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(HearthUi PRIVATE cxx_std_20)

target_link_libraries(HearthUi
    PUBLIC
        HearthModel
        HearthPlugins
        juce::juce_gui_basics
    PRIVATE
        juce::juce_recommended_warning_flags
)
