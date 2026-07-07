juce_add_console_app(HowlHost
    PRODUCT_NAME "Howl Host"
    COMPANY_NAME "Howl"
)

target_sources(HowlHost PRIVATE
    Main.cpp
)

target_compile_features(HowlHost PRIVATE cxx_std_20)

# This console app pumps the JUCE message loop by hand (editor windows and timers need
# one), rather than through a juce::JUCEApplication's native run loop, which needs modal
# loops enabled so MessageManager::runDispatchLoopUntil() is available
target_compile_definitions(HowlHost PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)

find_package(Threads REQUIRED)

target_link_libraries(HowlHost PRIVATE
    HowlPlugins
    juce::juce_audio_processors
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_warning_flags
    Threads::Threads
)
