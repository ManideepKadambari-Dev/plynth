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
    GeneratorCliTests.cpp
    GeneratorBuildCliTests.cpp
    BuildCliTests.cpp
    FloorCliTests.cpp
    StairCliTests.cpp
    RoofCliTests.cpp
    ExportCliTests.cpp
    ExportGroupsCliTests.cpp
    MaterialCliTests.cpp
    MaterialCatalogCliTests.cpp
    MaterialImportCliTests.cpp
    StoreImportCliTests.cpp
    StoreFailureCliTests.cpp
    StoreSourceCliTests.cpp
    BlenderOptionCliTests.cpp
    RecentCliTests.cpp
    EnvironmentCliTests.cpp
    ErrorListTests.cpp
    RenderCliTests.cpp
    RenderProcessTests.cpp
    GapCliTests.cpp
    PackCliTests.cpp
    SessionKeeperTests.cpp
    IdCliTests.cpp
    FormatCliTests.cpp
    LightCliTests.cpp
    TrimCliTests.cpp
    FinishCliTests.cpp
    VariationCliTests.cpp
    GameplayCliTests.cpp
    IdTestListener.cpp
    TestSetup.cpp
    TestSupport.cpp
)
target_link_libraries(plynth_cli_tests PRIVATE plynth::cli Catch2::Catch2WithMain
    nlohmann_json::nlohmann_json nlohmann_json_schema_validator::validator)
target_compile_definitions(plynth_cli_tests PRIVATE
    PLYNTH_EXPECTED_VERSION="${PROJECT_VERSION}"
    PLYNTH_SCHEMA_DIR="${PROJECT_SOURCE_DIR}/schema"
    PLYNTH_TEST_GENERATORS="${PROJECT_SOURCE_DIR}/tests/core/data/generators"
    PLYNTH_SAMPLE_PLAN="${PROJECT_SOURCE_DIR}/samples/plans/apartment-01.png"
    PLYNTH_SAMPLE_CHAIR_FOLDER="${PROJECT_SOURCE_DIR}/samples/models/chair"
    PLYNTH_RENDER_FIXTURES="${PROJECT_SOURCE_DIR}/tests/data/render"
    PLYNTH_FAKE_BLENDER="$<TARGET_FILE:plynth_fake_blender>"
    PLYNTH_LEAKY_BLENDER="$<TARGET_FILE:plynth_leaky_blender>")
add_dependencies(plynth_cli_tests plynth_fake_blender plynth_leaky_blender)

catch_discover_tests(plynth_cli_tests DISCOVERY_MODE PRE_TEST)
