find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)
find_package(Clipper2 CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
include(Catch)

add_executable(plynth_echo_lines tools/echo_lines/main.cpp)

add_executable(plynth_fake_blender tools/fake_blender/main.cpp tools/fake_blender/store_tasks.cpp tools/fake_blender/store_fetch_env.cpp)
target_link_libraries(plynth_fake_blender PRIVATE nlohmann_json::nlohmann_json shell32 advapi32)
set_target_properties(plynth_fake_blender PROPERTIES OUTPUT_NAME fake_blender)

add_executable(plynth_leaky_blender tools/leaky_blender/main.cpp)
target_link_libraries(plynth_leaky_blender PRIVATE nlohmann_json::nlohmann_json shell32)

add_executable(plynth_integrity_probe tools/integrity_probe/main.cpp)
target_link_libraries(plynth_integrity_probe PRIVATE advapi32)

add_executable(plynth_fake_store_helper tools/fake_store_helper/main.cpp)
target_link_libraries(plynth_fake_store_helper PRIVATE nlohmann_json::nlohmann_json ws2_32 shell32 user32)
set_target_properties(plynth_fake_store_helper PROPERTIES OUTPUT_NAME fake_store_helper)

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
    core/OutlineEdgeCaseTests.cpp
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
    core/RenderTests.cpp
    core/PlanPictureTests.cpp
    core/StatusTests.cpp
    core/PackTests.cpp
    core/UnpackTests.cpp
    core/GapTests.cpp
    core/JobProgressTests.cpp
    core/ExportJobTests.cpp
    core/ExportGroupsTests.cpp
    core/ExportOptionsTests.cpp
    core/KitTests.cpp
    core/ExportEdgeCaseTests.cpp
    core/ExportEditGuardTests.cpp
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
    core/FinishFormatTests.cpp
    core/BuildFinishTests.cpp
    core/LightCommandTests.cpp
    core/GameplayExportTests.cpp
    core/LicenseExportTests.cpp
    core/SampleItemsTests.cpp
    core/StairRailingsTests.cpp
    core/BuildResolveTests.cpp
    core/VariationCopiesTests.cpp
    core/AssetSupportsInfoTests.cpp
    core/ZipListingTests.cpp
    core/FabZipTests.cpp
    core/FabImporterTests.cpp
    core/IntegrationsTests.cpp
    core/BlenderKitTests.cpp
    core/StoreFetchTests.cpp
    core/ImportPreviewTests.cpp
    core/StoreImportTests.cpp
    core/StoreKeyLeakTests.cpp
    core/StoreFetchScriptTests.cpp
    core/ZipSafetyTests.cpp
    core/IntegrationsFileTests.cpp
    core/FabItemWordsTests.cpp
    core/InboxMovedPathTests.cpp
    core/FabLicenseTests.cpp
    core/FabOwnZipTests.cpp
    core/FabResolutionTests.cpp
    core/FabPackTests.cpp
    core/StoreSourceTests.cpp
    core/StoreFetchFolderTests.cpp
    core/LowIntegrityProcessTests.cpp
    core/BlenderKitAddressTests.cpp
    core/IdTests.cpp
    core/IdGroupTests.cpp
    core/IdTestListener.cpp
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

add_dependencies(plynth_core_tests plynth_echo_lines plynth_fake_blender plynth_leaky_blender plynth_integrity_probe
    plynth_fake_store_helper)
target_compile_definitions(plynth_core_tests PRIVATE
    PLYNTH_ECHO_LINES="$<TARGET_FILE:plynth_echo_lines>"
    PLYNTH_FAKE_BLENDER="$<TARGET_FILE:plynth_fake_blender>"
    PLYNTH_LEAKY_BLENDER="$<TARGET_FILE:plynth_leaky_blender>"
    PLYNTH_INTEGRITY_PROBE="$<TARGET_FILE:plynth_integrity_probe>"
    PLYNTH_FAKE_STORE_HELPER="$<TARGET_FILE:plynth_fake_store_helper>"
    PLYNTH_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# Update tests make keys and signatures in memory with libsodium and serve releases on 127.0.0.1 with
# cpp-httplib. httplib is for tests only and never reaches a shipped program.
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
file(GLOB PLYNTH_UPDATE_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/core/update/*.cpp)
target_sources(plynth_core_tests PRIVATE ${PLYNTH_UPDATE_TEST_SOURCES})
target_link_libraries(plynth_core_tests PRIVATE unofficial-sodium::sodium httplib::httplib ws2_32)

# A fake PlynthSetup.exe with a version resource of 0.2.0, and a program that starts it from inside a job.
add_executable(plynth_fake_installer tools/fake_installer/main.cpp tools/fake_installer/version.rc)
target_link_libraries(plynth_fake_installer PRIVATE nlohmann_json::nlohmann_json)
set_target_properties(plynth_fake_installer PROPERTIES OUTPUT_NAME fake_installer)
add_executable(plynth_update_launcher tools/update_launcher/main.cpp)
target_link_libraries(plynth_update_launcher PRIVATE plynth::core)
add_dependencies(plynth_core_tests plynth_fake_installer plynth_update_launcher)
target_compile_definitions(plynth_core_tests PRIVATE
    PLYNTH_FAKE_INSTALLER="$<TARGET_FILE:plynth_fake_installer>"
    PLYNTH_UPDATE_LAUNCHER="$<TARGET_FILE:plynth_update_launcher>")

# The end to end update driver links core's private test hooks. It is never staged, and it is built only
# when asked for, by the update test on GitHub.
option(PLYNTH_UPDATE_DRIVER "Build the end to end update test driver" OFF)
if(PLYNTH_UPDATE_DRIVER)
    add_subdirectory(tools/update_driver)
endif()

# Release programs no longer know the source tree, so the tests name the Blender scripts folder themselves.
catch_discover_tests(plynth_core_tests DISCOVERY_MODE PRE_TEST ADD_TAGS_AS_LABELS
    PROPERTIES ENVIRONMENT_MODIFICATION "PLYNTH_BLENDER_SCRIPTS=set:${PROJECT_SOURCE_DIR}/blender")

add_subdirectory(guide)
