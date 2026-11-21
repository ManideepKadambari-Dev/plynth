find_package(CLI11 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(plynth_cli STATIC
    src/Answer.cpp
    src/Cli.cpp
    src/Registry.cpp
    src/commands/EditCommands.cpp
    src/commands/HelpCommand.cpp
    src/commands/ProjectCommands.cpp
    src/commands/VersionCommand.cpp
)
add_library(plynth::cli ALIAS plynth_cli)

if(WIN32)
    target_sources(plynth_cli PRIVATE src/Console_win.cpp)
endif()

target_include_directories(plynth_cli PUBLIC include)
target_compile_features(plynth_cli PUBLIC cxx_std_20)
target_link_libraries(plynth_cli PUBLIC plynth::core
    PRIVATE CLI11::CLI11 nlohmann_json::nlohmann_json)

if(PLYNTH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
