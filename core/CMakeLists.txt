find_package(nlohmann_json CONFIG REQUIRED)

add_library(plynth_core STATIC
    src/Version.cpp
    src/PlanImage.cpp
    src/Status.cpp
    src/Plan.cpp
    src/Project.cpp
    src/ProjectJson.cpp
    src/Command.cpp
    src/CommandStack.cpp
    src/ProjectFiles.cpp
    src/Document.cpp
    src/EditSession.cpp
    src/FileIo.cpp
    src/Geometry.cpp
    src/Snapping.cpp
    src/RoomTypes.cpp
    src/Rooms.cpp
    src/commands/FloorCommands.cpp
    src/commands/PointCommands.cpp
    src/commands/WallCommands.cpp
    src/commands/SettingsCommands.cpp
    src/commands/RoomCommands.cpp
    src/commands/DeleteCommands.cpp
)
if(WIN32)
    target_sources(plynth_core PRIVATE src/Process_win.cpp)
endif()
add_library(plynth::core ALIAS plynth_core)

target_include_directories(plynth_core PUBLIC include)
target_compile_features(plynth_core PUBLIC cxx_std_20)
target_link_libraries(plynth_core PUBLIC nlohmann_json::nlohmann_json)
set_source_files_properties(src/Version.cpp PROPERTIES
    COMPILE_DEFINITIONS PLYNTH_VERSION_STRING="${PROJECT_VERSION}")
find_package(Clipper2 CONFIG REQUIRED)
target_link_libraries(plynth_core PRIVATE Clipper2::Clipper2)
