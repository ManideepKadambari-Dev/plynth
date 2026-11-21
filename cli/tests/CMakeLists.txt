find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)
include(Catch)

add_executable(plynth_cli_tests
    CliTests.cpp
    ProjectCliTests.cpp
    SessionCliTests.cpp
    TestSupport.cpp
)
target_link_libraries(plynth_cli_tests PRIVATE plynth::cli Catch2::Catch2WithMain
    nlohmann_json::nlohmann_json nlohmann_json_schema_validator::validator)
target_compile_definitions(plynth_cli_tests PRIVATE
    PLYNTH_EXPECTED_VERSION="${PROJECT_VERSION}"
    PLYNTH_SCHEMA_DIR="${PROJECT_SOURCE_DIR}/schema"
    PLYNTH_SAMPLE_PLAN="${PROJECT_SOURCE_DIR}/samples/plans/apartment-01.png")

catch_discover_tests(plynth_cli_tests)
