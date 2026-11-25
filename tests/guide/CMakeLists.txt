add_executable(plynth_guide_tests
    ../core/TestSetup.cpp
    ResourcesTests.cpp
    HelpPagesTests.cpp
    AboutTests.cpp
    SamplesTests.cpp
    LicenseReportTests.cpp
    LogoTests.cpp
    AuthorTests.cpp
)
target_link_libraries(plynth_guide_tests PRIVATE
    plynth::core
    Catch2::Catch2WithMain
    nlohmann_json_schema_validator::validator
    bcrypt)
target_compile_definitions(plynth_guide_tests PRIVATE
    PLYNTH_SCHEMA_DIR="${PROJECT_SOURCE_DIR}/schema"
    PLYNTH_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples"
    PLYNTH_DOCS_DIR="${PROJECT_SOURCE_DIR}/docs"
    PLYNTH_LICENSES_DIR="${PROJECT_SOURCE_DIR}/licenses"
    PLYNTH_APP_RESOURCES_DIR="${PROJECT_SOURCE_DIR}/app/resources"
    PLYNTH_APP_DIR="${PROJECT_SOURCE_DIR}/app"
    PLYNTH_CORE_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/core/data")

# Release programs no longer know the source tree, so the tests name the Blender scripts folder themselves.
catch_discover_tests(plynth_guide_tests DISCOVERY_MODE PRE_TEST ADD_TAGS_AS_LABELS
    PROPERTIES ENVIRONMENT_MODIFICATION "PLYNTH_BLENDER_SCRIPTS=set:${PROJECT_SOURCE_DIR}/blender")
