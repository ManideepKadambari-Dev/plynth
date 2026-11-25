# plynth.com: a console program that starts plynth.exe, placed next to it in the app build folder.
add_executable(plynth_launcher main.cpp)
target_link_libraries(plynth_launcher PRIVATE plynth::cli_words)
target_include_directories(plynth_launcher PRIVATE ${PROJECT_SOURCE_DIR}/core/include)

set_target_properties(plynth_launcher PROPERTIES
    OUTPUT_NAME plynth
    SUFFIX ".com"
    PDB_NAME plynth_launcher
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/app/$<CONFIG>")
target_link_options(plynth_launcher PRIVATE /INCREMENTAL:NO)

if(PLYNTH_BUILD_TESTS)
    find_package(Catch2 CONFIG REQUIRED)
    find_package(nlohmann_json CONFIG REQUIRED)
    include(Catch)

    add_executable(plynth_fake_child tests/FakeChild.cpp tests/TestSetup.cpp)
    set_target_properties(plynth_fake_child PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fake/$<CONFIG>")

    add_executable(plynth_launcher_tests tests/LauncherTests.cpp tests/TestSetup.cpp)
    add_dependencies(plynth_launcher_tests plynth_launcher plynth_fake_child)
    target_link_libraries(plynth_launcher_tests PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
    target_compile_definitions(plynth_launcher_tests PRIVATE
        PLYNTH_LAUNCHER_FILE="$<TARGET_FILE:plynth_launcher>"
        PLYNTH_FAKE_CHILD="$<TARGET_FILE:plynth_fake_child>")
    catch_discover_tests(plynth_launcher_tests DISCOVERY_MODE PRE_TEST)
endif()
