find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Quick QuickWidgets Quick3D)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(plynth WIN32
    src/main.cpp
    src/building/BuildController.h
    src/building/BuildController.cpp
    src/building/BuildPanel.h
    src/building/BuildPanel.cpp
    src/building/ProblemMark.h
    src/catalog/ImportQueue.h
    src/catalog/ImportQueue.cpp
    src/catalog/LibraryHost.h
    src/catalog/LibraryHost.cpp
    src/catalog/LibraryWatcher.h
    src/catalog/LibraryWatcher.cpp
    src/canvas/OpeningPaint.h
    src/canvas/OpeningPaint.cpp
    src/canvas/StairPaint.h
    src/canvas/StairPaint.cpp
    src/canvas/OverlayBuilder.h
    src/canvas/OverlayBuilder.cpp
    src/canvas/OverlayOpenings.cpp
    src/canvas/PlanCanvas.h
    src/canvas/PlanCanvas.cpp
    src/detection/DetectionRunner.h
    src/detection/DetectionRunner.cpp
    src/detection/DetectionSession.h
    src/detection/DetectionSession.cpp
    src/home/HomeScreen.h
    src/home/HomeScreen.cpp
    src/home/ProjectThumbnail.h
    src/home/ProjectThumbnail.cpp
    src/materials/MaterialImportQueue.h
    src/materials/MaterialImportQueue.cpp
    src/materials/MaterialImportScreen.h
    src/materials/MaterialImportScreen.cpp
    src/materials/MaterialPicker.h
    src/materials/MaterialPicker.cpp
    src/materials/MaterialPreview.h
    src/materials/MaterialPreview.cpp
    src/materials/MaterialRefresher.h
    src/materials/MaterialRefresher.cpp
    src/materials/MaterialRow.h
    src/materials/MaterialRow.cpp
    src/furniture/AssetPreview.h
    src/furniture/AssetPreview.cpp
    src/furniture/FurniturePicker.h
    src/furniture/FurniturePicker.cpp
    src/dialogs/RoomTypesDialog.h
    src/dialogs/RoomTypesDialog.cpp
    src/dialogs/ScaleDialog.h
    src/dialogs/ScaleDialog.cpp
    src/document/FloorFocus.h
    src/document/FloorFocus.cpp
    src/document/Selection.h
    src/document/Selection.cpp
    src/document/DocumentBridge.h
    src/document/DocumentBridge.cpp
    src/document/ProjectWatcher.h
    src/document/ProjectWatcher.cpp
    src/preview/AssetStage.h
    src/preview/AssetStage.cpp
    src/preview/OverlayRenderer.h
    src/preview/OverlayRenderer.cpp
    src/preview/GraphicsProbe.h
    src/preview/GraphicsProbe.cpp
    src/preview/ModelView.h
    src/preview/ModelView.cpp
    src/preview/PreviewView.h
    src/preview/PreviewView.cpp
    src/preview/ThemeQml.h
    src/preview/ThemeQml.cpp
    src/panels/AssetsPanel.h
    src/panels/AssetsPanel.cpp
    src/panels/MaterialsPanel.h
    src/panels/MaterialsPanel.cpp
    src/panels/ImportQueueView.h
    src/panels/ImportQueueView.cpp
    src/panels/PropertiesPanel.h
    src/panels/PropertiesPanel.cpp
    src/panels/RightPanel.h
    src/panels/RightPanel.cpp
    src/panels/ToolStrip.h
    src/panels/ToolStrip.cpp
    src/tools/FurnitureTool.h
    src/tools/FurnitureTool.cpp
    src/tools/DetectTool.h
    src/tools/DetectTool.cpp
    src/tools/OpeningTool.h
    src/tools/OpeningTool.cpp
    src/tools/RoomTool.h
    src/tools/RoomTool.cpp
    src/tools/ScaleTool.h
    src/tools/ScaleTool.cpp
    src/tools/StairTool.h
    src/tools/StairTool.cpp
    src/tools/SelectTool.h
    src/tools/SelectTool.cpp
    src/tools/Tool.h
    src/tools/ToolController.h
    src/tools/ToolController.cpp
    src/tools/WallTool.h
    src/tools/WallTool.cpp
    src/setup/SetupCheckDialog.h
    src/setup/SetupCheckDialog.cpp
    src/terminal/TerminalDock.h
    src/terminal/TerminalDock.cpp
    src/terminal/TerminalWidgets.h
    src/terminal/TerminalWidgets.cpp
    src/theme/Theme.h
    src/theme/Theme.cpp
    src/wallview/WallFaceView.h
    src/wallview/WallFaceView.cpp
    src/wallview/WallView.h
    src/wallview/WallView.cpp
    src/wallview/WallViewData.h
    src/wallview/WallViewData.cpp
    src/window/FloorTabs.h
    src/window/FloorTabs.cpp
    src/window/BuildBar.h
    src/window/BuildBar.cpp
    src/window/MainWindow.h
    src/window/MainWindow.cpp
    src/window/MainWindowBuild.cpp
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
    target_link_libraries(plynth PRIVATE dwmapi user32 d3d11 dxgi)
endif()

target_include_directories(plynth PRIVATE src)
target_link_libraries(plynth PRIVATE plynth::core plynth::cli plynth::terminal Qt6::Widgets Qt6::Quick Qt6::QuickWidgets Qt6::Quick3D)

# Copy the Qt DLLs and plugins next to plynth.exe so it runs from the build folder.
get_target_property(PLYNTH_QMAKE Qt6::qmake IMPORTED_LOCATION)
get_filename_component(PLYNTH_QT_BIN "${PLYNTH_QMAKE}" DIRECTORY)
find_program(PLYNTH_WINDEPLOYQT windeployqt HINTS "${PLYNTH_QT_BIN}" NO_DEFAULT_PATH)
if(PLYNTH_WINDEPLOYQT)
    add_custom_command(TARGET plynth POST_BUILD
        COMMAND "${PLYNTH_WINDEPLOYQT}"
            $<IF:$<CONFIG:Debug>,--debug,--release>
            --no-translations --no-compiler-runtime --no-system-d3d-compiler
            --no-opengl-sw --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml" --dir "$<TARGET_FILE_DIR:plynth>"
            "$<TARGET_FILE:plynth>"
        COMMENT "Running windeployqt"
    )
else()
    message(WARNING "windeployqt not found, plynth.exe will need Qt on PATH")
endif()
