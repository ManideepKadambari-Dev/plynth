find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(plynth WIN32
    src/main.cpp
    src/canvas/PlanCanvas.h
    src/canvas/PlanCanvas.cpp
    src/document/DocumentBridge.h
    src/document/DocumentBridge.cpp
    src/document/ProjectWatcher.h
    src/document/ProjectWatcher.cpp
    src/terminal/TerminalDock.h
    src/terminal/TerminalDock.cpp
    src/terminal/TerminalWidgets.h
    src/terminal/TerminalWidgets.cpp
    src/theme/Theme.h
    src/theme/Theme.cpp
    src/window/MainWindow.h
    src/window/MainWindow.cpp
    src/window/ReadOnlyBanner.h
    src/window/ReadOnlyBanner.cpp
    src/window/StatusBar.h
    src/window/StatusBar.cpp
    src/window/TitleBar.h
    src/window/TitleBar.cpp
    resources/app.qrc
    resources/plynth.rc
)

if(WIN32)
    target_sources(plynth PRIVATE
        src/window/FramelessWindow_win.h
        src/window/FramelessWindow_win.cpp)
    target_link_libraries(plynth PRIVATE dwmapi user32)
endif()

target_include_directories(plynth PRIVATE src)
target_link_libraries(plynth PRIVATE plynth::core plynth::cli plynth::terminal Qt6::Widgets)

# Copy the Qt DLLs and plugins next to plynth.exe so it runs from the build folder.
get_target_property(PLYNTH_QMAKE Qt6::qmake IMPORTED_LOCATION)
get_filename_component(PLYNTH_QT_BIN "${PLYNTH_QMAKE}" DIRECTORY)
find_program(PLYNTH_WINDEPLOYQT windeployqt HINTS "${PLYNTH_QT_BIN}" NO_DEFAULT_PATH)
if(PLYNTH_WINDEPLOYQT)
    add_custom_command(TARGET plynth POST_BUILD
        COMMAND "${PLYNTH_WINDEPLOYQT}"
            $<IF:$<CONFIG:Debug>,--debug,--release>
            --no-translations --no-compiler-runtime --no-system-d3d-compiler
            --no-opengl-sw --dir "$<TARGET_FILE_DIR:plynth>"
            "$<TARGET_FILE:plynth>"
        COMMENT "Running windeployqt"
    )
else()
    message(WARNING "windeployqt not found, plynth.exe will need Qt on PATH")
endif()
