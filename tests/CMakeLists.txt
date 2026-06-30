add_executable(HearthTests
    GraphTests.cpp
    TransportTests.cpp
)

target_compile_features(HearthTests PRIVATE cxx_std_20)

target_link_libraries(HearthTests PRIVATE
    HearthEngine
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(HearthTests)
