find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)
find_package(Clipper2 CONFIG REQUIRED)
include(Catch)

add_executable(plynth_echo_lines tools/echo_lines/main.cpp)

add_executable(plynth_fake_blender tools/fake_blender/main.cpp)
target_link_libraries(plynth_fake_blender PRIVATE nlohmann_json::nlohmann_json shell32)
set_target_properties(plynth_fake_blender PROPERTIES OUTPUT_NAME fake_blender)

add_executable(plynth_core_tests
    core/TestSetup.cpp
    core/VersionTests.cpp
    core/SettingsTests.cpp
    core/ProcessTests.cpp
    core/BlenderTests.cpp
    core/BlenderJobTests.cpp
    core/SetupCheckTests.cpp
    core/ImporterTests.cpp
    core/AssetImportTests.cpp
    core/LibraryTests.cpp
    core/AssetInfoTests.cpp
    core/AssetCatalogTests.cpp
    core/ItemAssetsTests.cpp
    core/ItemsTests.cpp
    core/ItemCommandTests.cpp
    core/SearchTests.cpp
    core/RescanTests.cpp
    core/PlanImageTests.cpp
    core/PlanTests.cpp
    core/ProjectJsonTests.cpp
    core/SchemaTests.cpp
    core/CommandTests.cpp
    core/CommandStackTests.cpp
    core/ProjectFilesTests.cpp
    core/DocumentTests.cpp
    core/EditSessionTests.cpp
    core/GeometryTests.cpp
    core/OpeningShapeTests.cpp
    core/ShapeFileTests.cpp
    core/WallGeometryTests.cpp
    core/OpeningTests.cpp
    core/OpeningCommandTests.cpp
    core/BuildGeometryTests.cpp
    core/SnappingTests.cpp
    core/RoomTypesTests.cpp
    core/WallCommandTests.cpp
    core/RoomsTests.cpp
    core/RoomCommandTests.cpp
    core/DeleteCommandTests.cpp
    core/FakeDetector.cpp
    core/DetectionModelTests.cpp
    core/DetectionCommandTests.cpp
    core/GrayImageTests.cpp
    core/OrthogonalDetectorTests.cpp
    core/DetectionPerfTests.cpp
)
target_link_libraries(plynth_core_tests PRIVATE
    plynth::core
    Catch2::Catch2WithMain
    Clipper2::Clipper2
    nlohmann_json_schema_validator::validator)
target_compile_definitions(plynth_core_tests PRIVATE
    PLYNTH_EXPECTED_VERSION="${PROJECT_VERSION}"
    PLYNTH_SCHEMA_DIR="${PROJECT_SOURCE_DIR}/schema"
    PLYNTH_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples"
    PLYNTH_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/core/data")

add_dependencies(plynth_core_tests plynth_echo_lines plynth_fake_blender)
target_compile_definitions(plynth_core_tests PRIVATE
    PLYNTH_ECHO_LINES="$<TARGET_FILE:plynth_echo_lines>"
    PLYNTH_FAKE_BLENDER="$<TARGET_FILE:plynth_fake_blender>")

catch_discover_tests(plynth_core_tests)
