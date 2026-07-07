juce_add_console_app(HowlHost
    PRODUCT_NAME "Howl Host"
    COMPANY_NAME "Howl"
)

target_sources(HowlHost PRIVATE
    Main.cpp
)

target_compile_features(HowlHost PRIVATE cxx_std_20)

target_link_libraries(HowlHost PRIVATE
    HowlPlugins
    juce::juce_core
    juce::juce_recommended_warning_flags
)
