find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)
include(Catch)

add_executable(plynth_cli_tests
    CliTests.cpp
    ProjectCliTests.cpp
    SessionCliTests.cpp
    WallCliTests.cpp
    RoomCliTests.cpp
    DetectCliTests.cpp
    OpeningCliTests.cpp
    CurvedCliTests.cpp
    OpeningEditCliTests.cpp
    SettingsCliTests.cpp
    AssetCliTests.cpp
    LibraryCliTests.cpp
    DoctorCliTests.cpp
    ImportCliTests.cpp
    ItemCliTests.cpp
    BuildCliTests.cpp
    FloorCliTests.cpp
    StairCliTests.cpp
    RoofCliTests.cpp
    ExportCliTests.cpp
    ExportGroupsCliTests.cpp
    MaterialCliTests.cpp
    MaterialCatalogCliTests.cpp
    MaterialImportCliTests.cpp
    RecentCliTests.cpp
    EnvironmentCliTests.cpp
    IdCliTests.cpp
    IdTestListener.cpp
    TestSetup.cpp
    TestSupport.cpp
)
target_link_libraries(plynth_cli_tests PRIVATE plynth::cli Catch2::Catch2WithMain
    nlohmann_json::nlohmann_json nlohmann_json_schema_validator::validator)
target_compile_definitions(plynth_cli_tests PRIVATE
    PLYNTH_EXPECTED_VERSION="${PROJECT_VERSION}"
    PLYNTH_SCHEMA_DIR="${PROJECT_SOURCE_DIR}/schema"
    PLYNTH_SAMPLE_PLAN="${PROJECT_SOURCE_DIR}/samples/plans/apartment-01.png"
    PLYNTH_SAMPLE_CHAIR_FOLDER="${PROJECT_SOURCE_DIR}/samples/models/chair"
    PLYNTH_FAKE_BLENDER="$<TARGET_FILE:plynth_fake_blender>")
add_dependencies(plynth_cli_tests plynth_fake_blender)

catch_discover_tests(plynth_cli_tests DISCOVERY_MODE PRE_TEST)
