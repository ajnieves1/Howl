juce_add_gui_app(Howl
    PRODUCT_NAME "Howl"
    COMPANY_NAME "Howl"
    BUNDLE_ID    "org.howl.daw"
)

target_sources(Howl PRIVATE
    Main.cpp
)

target_compile_features(Howl PRIVATE cxx_std_20)

target_link_libraries(Howl PRIVATE
    HowlDsp
    HowlEngine
    HowlIo
    HowlModel
    HowlUi
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
