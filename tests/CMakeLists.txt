find_package(Catch2 CONFIG REQUIRED)
include(Catch)

add_executable(plynth_core_tests
    core/VersionTests.cpp
    core/PlanImageTests.cpp
)
target_link_libraries(plynth_core_tests PRIVATE plynth::core Catch2::Catch2WithMain)
target_compile_definitions(plynth_core_tests PRIVATE PLYNTH_EXPECTED_VERSION="${PROJECT_VERSION}")

catch_discover_tests(plynth_core_tests)
