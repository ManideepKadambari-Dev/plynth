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
    src/WallGeometry.cpp
    src/Openings.cpp
    src/openings/OpeningShapes.cpp
    src/openings/ShapeFiles.cpp
    src/commands/FloorCommands.cpp
    src/commands/PointCommands.cpp
    src/commands/WallCommands.cpp
    src/commands/SettingsCommands.cpp
    src/commands/RoomCommands.cpp
    src/commands/DeleteCommands.cpp
    src/commands/OpeningCommands.cpp
    src/commands/DetectionCommands.cpp
    src/detection/Detection.cpp
    src/detection/Detector.cpp
    src/detection/GrayImage.cpp
    src/detection/OrthogonalDetection.cpp
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
find_package(OpenCV CONFIG REQUIRED COMPONENTS opencv_core opencv_imgproc opencv_imgcodecs)
target_link_libraries(plynth_core PRIVATE opencv_core opencv_imgproc opencv_imgcodecs)

# Detectors register themselves from namespace scope objects. A static library drops object files nobody
# references, so detector files are built as an object library and their objects are added to every program
# that links core.
file(GLOB PLYNTH_DETECTOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/detection/detectors/*.cpp)
if(PLYNTH_DETECTOR_SOURCES)
    add_library(plynth_detectors OBJECT ${PLYNTH_DETECTOR_SOURCES})
    target_include_directories(plynth_detectors PRIVATE include src src/detection)
    target_link_libraries(plynth_detectors PRIVATE nlohmann_json::nlohmann_json Clipper2::Clipper2 opencv_core opencv_imgproc opencv_imgcodecs)
    target_compile_features(plynth_detectors PUBLIC cxx_std_20)
    target_sources(plynth_core INTERFACE $<TARGET_OBJECTS:plynth_detectors>)
endif()

# Opening shapes register themselves the same way, so they are built as an object library too.
file(GLOB PLYNTH_SHAPE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/openings/shapes/*.cpp)
if(PLYNTH_SHAPE_SOURCES)
    add_library(plynth_opening_shapes OBJECT ${PLYNTH_SHAPE_SOURCES})
    target_include_directories(plynth_opening_shapes PRIVATE include src)
    target_compile_features(plynth_opening_shapes PUBLIC cxx_std_20)
    target_link_libraries(plynth_opening_shapes PRIVATE nlohmann_json::nlohmann_json)
    target_sources(plynth_core INTERFACE $<TARGET_OBJECTS:plynth_opening_shapes>)
endif()
