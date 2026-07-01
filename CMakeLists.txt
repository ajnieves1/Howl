# cmake -B build -G Ninja
# cmake --build build
# winget install Microsoft.VisualStudio.2022.BuildTools
cmake_minimum_required(VERSION 3.22)
project(Howl VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.6.0
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG        1.2.9
    GIT_SHALLOW    TRUE
)

# clap-helpers has no tagged releases, so pin a specific main commit
FetchContent_Declare(
    clap-helpers
    GIT_REPOSITORY https://github.com/free-audio/clap-helpers.git
    GIT_TAG        fd5abecb9f79f7051f4397725baddd2fe7d57b7b
)

# clap must be made available before clap-helpers, which looks for the
# clap target at configure time and errors out if it is not defined yet
FetchContent_MakeAvailable(JUCE Catch2 clap clap-helpers)

# Treat the fetched CLAP headers as system headers so their own warnings
# stay out of our zero-warning build output (SYSTEM property is CMake 3.25+,
# silently ignored on older versions)
if (TARGET clap)
    set_target_properties(clap PROPERTIES SYSTEM TRUE)
endif()
if (TARGET clap-helpers)
    set_target_properties(clap-helpers PROPERTIES SYSTEM TRUE)
endif()

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

enable_testing()

# Applies to every target below so each JUCE module is compiled
# with the same flags no matter which of our libraries pulls it in
add_compile_definitions(
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_PLUGINHOST_VST3=1
)

add_subdirectory(src/io)
add_subdirectory(src/engine)
add_subdirectory(src/dsp)
add_subdirectory(src/plugins)
add_subdirectory(src/model)
add_subdirectory(src/ui)
add_subdirectory(src/app)
add_subdirectory(tests)
