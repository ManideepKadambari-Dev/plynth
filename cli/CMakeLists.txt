find_package(CLI11 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(plynth_cli_words STATIC src/Words.cpp)
add_library(plynth::cli_words ALIAS plynth_cli_words)
target_include_directories(plynth_cli_words PUBLIC include)
target_compile_features(plynth_cli_words PUBLIC cxx_std_20)

add_library(plynth_cli STATIC
    src/AgentGuide.cpp
    src/Answer.cpp
    src/CatalogCommon.cpp
    src/Cli.cpp
    src/Common.cpp
    src/ErrorList.cpp
    src/Registry.cpp
    src/commands/AgentGuideCommands.cpp
    src/commands/AssetCommands.cpp
    src/commands/BuildCommands.cpp
    src/commands/FloorCommands.cpp
    src/commands/GeneratorCommands.cpp
    src/commands/LibraryCommands.cpp
    src/commands/MaterialCommands.cpp
    src/commands/MaterialCatalogCommands.cpp
    src/commands/MaterialImportCommands.cpp
    src/commands/OpeningCommands.cpp
    src/commands/PackCommands.cpp
    src/commands/RenderCommands.cpp
    src/commands/PointCommands.cpp
    src/commands/DetectCommands.cpp
    src/commands/DoctorCommands.cpp
    src/commands/ExportCommands.cpp
    src/commands/HelpCommand.cpp
    src/commands/ImportCommands.cpp
    src/commands/ItemCommands.cpp
    src/commands/ProjectCommands.cpp
    src/commands/RoofCommands.cpp
    src/commands/RoomCommands.cpp
    src/commands/ScaleCommands.cpp
    src/commands/SessionCommands.cpp
    src/commands/SettingsCommands.cpp
    src/commands/StairCommands.cpp
    src/commands/RailingCommands.cpp
    src/commands/UpdateCommands.cpp
    src/commands/VersionCommand.cpp
    src/commands/WallCommands.cpp
    src/commands/LightCommands.cpp
    src/commands/TrimCommands.cpp
    src/commands/VariationCommands.cpp
    src/commands/GuideCommands.cpp
)
add_library(plynth::cli ALIAS plynth_cli)

if(WIN32)
    target_sources(plynth_cli PRIVATE src/Console_win.cpp)
endif()

target_include_directories(plynth_cli PUBLIC include)
target_compile_features(plynth_cli PUBLIC cxx_std_20)
target_link_libraries(plynth_cli PUBLIC plynth::core plynth::cli_words
    PRIVATE CLI11::CLI11 nlohmann_json::nlohmann_json)

if(PLYNTH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(WIN32)
    add_subdirectory(launcher)
endif()
