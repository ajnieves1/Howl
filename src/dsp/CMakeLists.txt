add_library(HowlDsp STATIC
    BuiltInEffectFactory.cpp
    Compressor.cpp
    Delay.cpp
    Equalizer.cpp
    GainEffect.cpp
    Limiter.cpp
    OfflineStretcher.cpp
    Reverb.cpp
    SubtractiveSynth.cpp
)

target_include_directories(HowlDsp PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(HowlDsp PRIVATE cxx_std_20)

target_link_libraries(HowlDsp
    PUBLIC
        HowlEngine
    PRIVATE
        rubberband
        juce::juce_recommended_warning_flags
)
