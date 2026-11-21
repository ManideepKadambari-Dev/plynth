find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)
include(Catch)

add_executable(plynth_core_tests
    core/VersionTests.cpp
    core/PlanImageTests.cpp
    core/PlanTests.cpp
    core/ProjectJsonTests.cpp
    core/SchemaTests.cpp
    core/CommandTests.cpp
    core/CommandStackTests.cpp
    core/ProjectFilesTests.cpp
    core/DocumentTests.cpp
)
target_link_libraries(plynth_core_tests PRIVATE
    plynth::core
    Catch2::Catch2WithMain
    nlohmann_json_schema_validator::validator)
target_compile_definitions(plynth_core_tests PRIVATE
    PLYNTH_EXPECTED_VERSION="${PROJECT_VERSION}"
    PLYNTH_SCHEMA_DIR="${PROJECT_SOURCE_DIR}/schema"
    PLYNTH_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/core/data")

catch_discover_tests(plynth_core_tests)
