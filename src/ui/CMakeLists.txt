add_library(HowlUi STATIC
    ArrangeView.cpp
    ChannelStripComponent.cpp
    EffectEditorWindow.cpp
    MainComponent.cpp
    MixerView.cpp
    PianoRoll.cpp
    PluginWindow.cpp
    TransportBar.cpp
)

target_include_directories(HowlUi PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(HowlUi PRIVATE cxx_std_20)

target_link_libraries(HowlUi
    PUBLIC
        HowlModel
        HowlPlugins
        juce::juce_gui_basics
    PRIVATE
        juce::juce_recommended_warning_flags
)
