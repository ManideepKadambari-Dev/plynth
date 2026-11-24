find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)
find_package(Clipper2 CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
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
    core/MaterialInfoTests.cpp
    core/MaterialCatalogTests.cpp
    core/MaterialJobsTests.cpp
    core/MaterialImportTests.cpp
    core/ItemAssetsTests.cpp
    core/ItemsTests.cpp
    core/GeneratorsTests.cpp
    core/AddonsTests.cpp
    core/AddonFolderTests.cpp
    core/GeneratorBuildTests.cpp
    core/GeneratorJobTests.cpp
    core/GeneratorCommandsTests.cpp
    core/SampleGeneratorsTests.cpp
    core/ItemCommandTests.cpp
    core/SearchTests.cpp
    core/RescanTests.cpp
    core/PlanImageTests.cpp
    core/PlanTests.cpp
    core/ProjectJsonTests.cpp
    core/MaterialsTests.cpp
    core/MaterialCommandTests.cpp
    core/SchemaTests.cpp
    core/DoorApplyTests.cpp
    core/RotatedBuildingTests.cpp
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
    core/BuildJobTests.cpp
    core/BuildTests.cpp
    core/BuildPreviewTests.cpp
    core/StairsTests.cpp
    core/StairCommandTests.cpp
    core/FloorCommandTests.cpp
    core/StoreysTests.cpp
    core/RoofsTests.cpp
    core/RoofCommandTests.cpp
    core/CollisionTests.cpp
    core/MaterialSlotsTests.cpp
    core/ExportTests.cpp
    core/ExportJobTests.cpp
    core/BuildAssetMapsTests.cpp
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
    core/FinishHeadersTests.cpp
    core/SeededChoiceTests.cpp
    core/AssetSupportsTests.cpp
    core/TrimProfileTests.cpp
    core/GameplayDataTests.cpp
    core/OpeningAssetsTests.cpp
    core/DressingTests.cpp
    core/TrimTests.cpp
    core/VariationTests.cpp
)
target_link_libraries(plynth_core_tests PRIVATE
    plynth::core
    Catch2::Catch2WithMain
    Clipper2::Clipper2
    unofficial::sqlite3::sqlite3
    nlohmann_json_schema_validator::validator)
target_compile_definitions(plynth_core_tests PRIVATE
    PLYNTH_EXPECTED_VERSION="${PROJECT_VERSION}"
    PLYNTH_SCHEMA_DIR="${PROJECT_SOURCE_DIR}/schema"
    PLYNTH_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples"
    PLYNTH_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/core/data")

if(PLYNTH_DEV_SAMPLES STREQUAL "ON")
    set(PLYNTH_TEST_DEV_SAMPLES 1)
elseif(PLYNTH_DEV_SAMPLES STREQUAL "OFF")
    set(PLYNTH_TEST_DEV_SAMPLES 0)
else()
    set(PLYNTH_TEST_DEV_SAMPLES "$<CONFIG:Debug>")
endif()
target_compile_definitions(plynth_core_tests PRIVATE PLYNTH_DEV_SAMPLES=${PLYNTH_TEST_DEV_SAMPLES})

add_dependencies(plynth_core_tests plynth_echo_lines plynth_fake_blender)
target_compile_definitions(plynth_core_tests PRIVATE
    PLYNTH_ECHO_LINES="$<TARGET_FILE:plynth_echo_lines>"
    PLYNTH_FAKE_BLENDER="$<TARGET_FILE:plynth_fake_blender>")

catch_discover_tests(plynth_core_tests DISCOVERY_MODE PRE_TEST ADD_TAGS_AS_LABELS)
