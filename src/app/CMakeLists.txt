juce_add_gui_app(Hearth
    PRODUCT_NAME "Hearth"
    COMPANY_NAME "Hearth"
    BUNDLE_ID    "org.hearth.daw"
)

target_sources(Hearth PRIVATE
    Main.cpp
)

target_compile_features(Hearth PRIVATE cxx_std_20)

target_link_libraries(Hearth PRIVATE
    HearthDsp
    HearthEngine
    HearthIo
    HearthModel
    HearthUi
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
