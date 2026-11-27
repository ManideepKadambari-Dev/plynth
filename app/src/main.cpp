#include "panels/ExportPanel.h"
#include "theme/Theme.h"
#include "catalog/LibraryHost.h"
#include "window/MainWindow.h"
#include "guide/GuideHost.h"
#include "preview/RenderHost.h"
#include "update/UpdateController.h"

#include <plynth/cli/Cli.h>
#include <plynth/core/Settings.h>
#include <plynth/core/Update.h>
#include <plynth/core/Version.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QImage>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>

#include <memory>

#include <cstdio>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

constexpr int kExitSaved = 0;
constexpr int kExitSaveFailed = 1;
constexpr int kExitBadArguments = 2;
constexpr int kExitImageFailed = 3;
constexpr int kExitTerminalFailed = 4;
constexpr int kExitUpdateInProgress = 5;
constexpr qint64 kDetectionWaitMs = 15000;
constexpr qint64 kSetupWaitMs = 60000;
constexpr qint64 kPreviewWaitMs = 30000;
constexpr qint64 kBuildWaitMs = 1800000;

// Reads "WxH" into a size. Returns false when the text is not two positive numbers.
bool parseSize(const QString& text, QSize& out)
{
    static const QRegularExpression pattern(QStringLiteral("^(\\d+)x(\\d+)$"));
    const QRegularExpressionMatch m = pattern.match(text);
    if (!m.hasMatch())
        return false;
    const int w = m.captured(1).toInt();
    const int h = m.captured(2).toInt();
    if (w < 1 || h < 1)
        return false;
    out = QSize(w, h);
    return true;
}

// Saves a picture of the window at exactly the requested pixel size, or at its own size when none is given.
bool saveScreenshot(plynth::app::MainWindow& window, const QString& file, const QSize& size)
{
    QImage image = window.grabPicture();
    if (size.isValid() && image.size() != size)
        image = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    image.setDevicePixelRatio(1.0);
    return image.save(file, "PNG");
}

// Writes the one JSON answer of a command that cannot run while setup runs, in the form the command line uses,
// to the redirected output or the console that started it.
void answerRefusedCommand(const char* command, const plynth::Status& status)
{
    plynth::Json error = plynth::Json::object();
    error["code"] = std::string(plynth::errorCodeName(status.code));
    error["message"] = status.message;
    plynth::Json answer = plynth::Json::object();
    answer["ok"] = false;
    answer["command"] = std::string(command);
    answer["error"] = std::move(error);
    const std::string text = answer.dump(-1, ' ', false, plynth::Json::error_handler_t::replace) + "\n";
#ifdef _WIN32
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    bool owns = false;
    if (out == nullptr || out == INVALID_HANDLE_VALUE || GetFileType(out) == FILE_TYPE_UNKNOWN) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS))
            return;
        out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0,
                          nullptr);
        if (out == INVALID_HANDLE_VALUE)
            return;
        SetConsoleOutputCP(CP_UTF8);
        owns = true;
    }
    std::size_t done = 0;
    while (done < text.size()) {
        DWORD written = 0;
        if (!WriteFile(out, text.data() + done, static_cast<DWORD>(text.size() - done), &written, nullptr) ||
            written == 0)
            break;
        done += written;
    }
    if (owns)
        CloseHandle(out);
#else
    std::fputs(text.c_str(), stdout);
#endif
}

// Tells the user who started the window that Plynth is being installed, and offers nothing else.
void showUpdateInProgress(const plynth::Status& status)
{
    QMessageBox box;
    box.setIcon(QMessageBox::NoIcon);
    box.setTextFormat(Qt::PlainText);
    box.setWindowTitle(QObject::tr("Plynth is being updated"));
    box.setText(QString::fromUtf8(status.message.data(), static_cast<qsizetype>(status.message.size())) +
                QStringLiteral("\n\n") + QObject::tr("Start Plynth again when the installer has finished."));
    box.setStandardButtons(QMessageBox::Ok);
    box.button(QMessageBox::Ok)->setToolTip(QObject::tr("Close this message"));
    box.exec();
}

}

// Parses the options, opens the window and optionally saves a screenshot.
int main(int argc, char* argv[])
{
    if (argc > 1 && plynth::cli::isCommandWord(argv[1])) {
        // A packaged program marks itself running, and refuses to run while its installer replaces it.
        const plynth::Status packaged = plynth::beginPackagedRun();
        if (!packaged.ok()) {
            answerRefusedCommand(argv[1], packaged);
            return plynth::cli::kExitFailed;
        }
        // Only render gets the 3D view, so no other command starts Qt.
        plynth::cli::CliHost host;
        if (std::string_view(argv[1]) == "render")
            host.render = plynth::app::renderShot;
        return plynth::cli::runFromProcess(argc, argv, host);
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Plynth"));
    QApplication::setApplicationVersion(QString::fromUtf8(plynth::version().data(), static_cast<qsizetype>(plynth::version().size())));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Plynth"));
    parser.addPositionalArgument(QStringLiteral("project"),
                                 QStringLiteral("Project folder to open (optional)."));
    QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                        QStringLiteral("Save a picture of the window and exit."),
                                        QStringLiteral("file"));
    QCommandLineOption sizeOption(QStringLiteral("size"),
                                  QStringLiteral("Window size in pixels, for example 1280x720."),
                                  QStringLiteral("WxH"));
    QCommandLineOption imageOption(QStringLiteral("image"),
                                   QStringLiteral("Plan image to open."),
                                   QStringLiteral("file"));
    QCommandLineOption terminalOption(QStringLiteral("terminal"),
                                      QStringLiteral("Open the terminal and type this text. For checks only."),
                                      QStringLiteral("text"));
    QCommandLineOption toolOption(QStringLiteral("tool"),
                                  QStringLiteral("Activate a tool: select, scale, wall, opening, room, stair, light, furniture or detect. For checks only."),
                                  QStringLiteral("name"));
    QCommandLineOption wallCheckOption(QStringLiteral("wall-check"),
                                       QStringLiteral("With --screenshot, drive the plan with the mouse: guides:x1,y1,x2,y2, join:point,x,y "
                                                      "or split:x,y, in metres. Writes <file>.txt and <file>-middle.png. For checks only."),
                                       QStringLiteral("spec"));
    QCommandLineOption selectOption(QStringLiteral("select"),
                                    QStringLiteral("Select the point, wall, opening, room, item, stair or light with this id, or first-wall. For checks only."),
                                    QStringLiteral("id"));
    QCommandLineOption floorOption(QStringLiteral("floor"),
                                   QStringLiteral("Show the floor with this id, such as f2. For checks only."), QStringLiteral("id"));
    QCommandLineOption wallSideOption(QStringLiteral("wall-side"),
                                      QStringLiteral("Side of the wall the wall view is seen from: left or right. For checks only."),
                                      QStringLiteral("side"));
    QCommandLineOption catalogOption(QStringLiteral("catalog"),
                                     QStringLiteral("Open the Assets window. For checks only."));
    QCommandLineOption selectAssetOption(QStringLiteral("select-asset"),
                                         QStringLiteral("Select the asset with this id in the Assets window. For checks only."),
                                         QStringLiteral("id"));
    QCommandLineOption assetCategoriesOption(QStringLiteral("asset-categories"),
                                             QStringLiteral("With --select-asset, scroll the details to the categories. For checks only."));
    QCommandLineOption materialsTabOption(QStringLiteral("materials-tab"),
                                          QStringLiteral("Open the Materials window. For checks only."));
    QCommandLineOption selectMaterialOption(QStringLiteral("select-material"),
                                            QStringLiteral("Select the material with this id in the Materials window. For checks only."),
                                            QStringLiteral("id"));
    QCommandLineOption materialPickerOption(QStringLiteral("material-picker"),
                                            QStringLiteral("Open the material picker for a target such as w3.outside, r2.floor or default.floor. For checks only."),
                                            QStringLiteral("target"));
    QCommandLineOption hoverMaterialOption(QStringLiteral("hover-material"),
                                           QStringLiteral("With --material-picker, hover the material with this id. For checks only."),
                                           QStringLiteral("id"));
    QCommandLineOption pickMaterialOption(QStringLiteral("pick-material"),
                                          QStringLiteral("With --material-picker, choose the material with this id as a click would. For checks only."),
                                          QStringLiteral("id"));
    QCommandLineOption setupOption(QStringLiteral("setup"), QStringLiteral("Open the setup check. For checks only."));
    QCommandLineOption setupItemOption(QStringLiteral("setup-item"),
                                       QStringLiteral("Open the setup check scrolled to the item with this title. For checks only."),
                                       QStringLiteral("title"));
    QCommandLineOption importOption(QStringLiteral("import"),
                                    QStringLiteral("Queue an import of this file or folder. For checks only."),
                                    QStringLiteral("file"));
    QCommandLineOption importMaterialsOption(QStringLiteral("import-materials"),
                                             QStringLiteral("Open the Import materials window. For checks only."));
    QCommandLineOption materialSourceOption(QStringLiteral("material-source"),
                                            QStringLiteral("With --import-materials, add this file or folder to the list. For checks only."),
                                            QStringLiteral("path"));
    QCommandLineOption addAssetMaterialOption(QStringLiteral("add-asset-material"),
                                              QStringLiteral("Add a material of an asset to the materials catalog, as asset/material, as the button does. For checks only."),
                                              QStringLiteral("asset/material"));
    QCommandLineOption addSamplesOption(QStringLiteral("add-sample-materials"),
                                        QStringLiteral("Import the sample materials as the button does. For checks only."));
    QCommandLineOption furnitureAtOption(QStringLiteral("furniture-at"),
                                         QStringLiteral("Open the furniture picker at x,y in metres on the plan. For checks only."),
                                         QStringLiteral("x,y"));
    QCommandLineOption hoverAssetOption(QStringLiteral("hover-asset"),
                                        QStringLiteral("With --furniture-at, hover the asset with this id. For checks only."),
                                        QStringLiteral("id"));
    QCommandLineOption viewOption(QStringLiteral("view"),
                                  QStringLiteral("Show the plan or the 3D page: plan or 3d. For checks only."),
                                  QStringLiteral("name"));
    QCommandLineOption cameraOption(QStringLiteral("camera"),
                                    QStringLiteral("Camera of the 3D page: three-quarter or top. For checks only."),
                                    QStringLiteral("name"));
    QCommandLineOption zoomOption(QStringLiteral("zoom"),
                                  QStringLiteral("Zoom of the plan: fit or actual (100 percent). For checks only."),
                                  QStringLiteral("mode"));
    QCommandLineOption overlayOption(QStringLiteral("overlay-3d"),
                                     QStringLiteral("Show the 3D overlay on the plan. For checks only."));
    QCommandLineOption buildOption(QStringLiteral("build"),
                                   QStringLiteral("Build the project after opening it and wait for the end. For checks only."));
    QCommandLineOption swapBuildOption(QStringLiteral("swap-build"),
                                       QStringLiteral("After the 3D page has drawn the building, copy this build folder over "
                                                      "the project's build and load it as after a build. For checks only."),
                                       QStringLiteral("folder"));
    parser.addOption(swapBuildOption);
    QCommandLineOption flyCheckOption(QStringLiteral("fly-check"),
                                      QStringLiteral("After the 3D page has drawn the building, fly with the right button: "
                                                     "look, move or frame. For checks only."),
                                      QStringLiteral("mode"));
    parser.addOption(flyCheckOption);
    QCommandLineOption buildTabOption(QStringLiteral("build-tab"), QStringLiteral("Open the Build report window. For checks only."));
    QCommandLineOption alignImageOption(QStringLiteral("align-image"),
                                        QStringLiteral("Turn on image alignment of the shown floor. For checks only."));
    QCommandLineOption roofSectionsOption(QStringLiteral("roof-sections"),
                                          QStringLiteral("Show the roof sections of the shown floor on the plan. For checks only."));
    QCommandLineOption selectRoofOption(QStringLiteral("select-roof"),
                                        QStringLiteral("Select the roof section <floor>:<index>, such as f2:0, on its floor. For checks only."),
                                        QStringLiteral("id"));
    QCommandLineOption noRoofsOption(QStringLiteral("no-roofs"),
                                     QStringLiteral("Hide the roofs in the 3D view. For checks only."));
    QCommandLineOption cutAtFloorOption(QStringLiteral("cut-at-floor"),
                                        QStringLiteral("Cut the 3D view at the shown floor. For checks only."));
    QCommandLineOption exportOption(QStringLiteral("export"),
                                    QStringLiteral("Export the last build after opening the project and wait for the end. For checks only."));
    QCommandLineOption exportCheckOption(QStringLiteral("export-check"),
                                         QStringLiteral("Run the export quality checks after opening the project and wait for the end. For checks only."));
    QCommandLineOption exportTabOption(QStringLiteral("export-tab"), QStringLiteral("Open the Export window. For checks only."));
    QCommandLineOption packDialogOption(QStringLiteral("pack-dialog"),
                                        QStringLiteral("Show the pack or the unpack dialog: pack or unpack. For checks only."),
                                        QStringLiteral("kind"));
    parser.addOption(packDialogOption);
    QCommandLineOption integrationsTabOption(QStringLiteral("integrations-tab"),
                                             QStringLiteral("Open the Sources window. For checks only."));
    QCommandLineOption blenderKitAddressOption(QStringLiteral("blenderkit-address"),
                                               QStringLiteral("Fill the BlenderKit address field of the Sources window. For checks only."),
                                               QStringLiteral("text"));
    QCommandLineOption fabImportOption(QStringLiteral("fab-import"),
                                       QStringLiteral("Open the Fab import dialog for this model zip. For checks only."),
                                       QStringLiteral("zip"));
    QCommandLineOption integrationFileOption(QStringLiteral("integration-file"),
                                             QStringLiteral("Followed by an integration id and a file: does what its import button does with that file. For checks only."),
                                             QStringLiteral("id"));
    QCommandLineOption exportGroupsExpandedOption(QStringLiteral("export-groups-expanded"),
                                                  QStringLiteral("Open every group in the Export window. For checks only."));
    QCommandLineOption lightToolOption(QStringLiteral("light-tool"), QStringLiteral("Activate the Light tool. For checks only."));
    QCommandLineOption addLightOption(QStringLiteral("add-light"),
                                      QStringLiteral("Place a light at x,y in metres on the shown floor, with an optional kind "
                                                     "(ceiling, wall or standing), and select it. Repeat for several. For checks only."),
                                      QStringLiteral("x,y[,kind]"));
    QCommandLineOption projectFinishOption(QStringLiteral("project-finish"),
                                           QStringLiteral("Show the Trim and Variation groups of the project. For checks only."));
    QCommandLineOption variationsDialogOption(QStringLiteral("variations-dialog"),
                                              QStringLiteral("Open Build > Make variations. For checks only."));
    QCommandLineOption openingAssetPickerOption(QStringLiteral("opening-asset-picker"),
                                                QStringLiteral("Open the door and window picker of the selected opening, or of the "
                                                               "first door. For checks only."));
    parser.addOption(lightToolOption);
    parser.addOption(addLightOption);
    parser.addOption(projectFinishOption);
    QCommandLineOption exportLicensesOnOption(QStringLiteral("export-licenses-on"),
                                              QStringLiteral("Tick the license report box of the Export window and open the window. For checks only."));
    parser.addOption(exportLicensesOnOption);
    parser.addOption(variationsDialogOption);
    parser.addOption(openingAssetPickerOption);
    QCommandLineOption homeOption(QStringLiteral("home"), QStringLiteral("Show the home screen. For checks only."));
    QCommandLineOption newProjectDialogOption(QStringLiteral("new-project-dialog"),
                                              QStringLiteral("Show the New project dialog. For checks only."));
    QCommandLineOption newProjectNameOption(QStringLiteral("new-project-name"),
                                            QStringLiteral("Type this name in the New project dialog. For checks only."),
                                            QStringLiteral("text"));
    QCommandLineOption newProjectLocationOption(QStringLiteral("new-project-location"),
                                                QStringLiteral("Type this location in the New project dialog. For checks only."),
                                                QStringLiteral("folder"));
    QCommandLineOption floorMenuOption(QStringLiteral("floor-menu"),
                                       QStringLiteral("Open the menu of this floor's tab. For checks only."),
                                       QStringLiteral("floor id"));
    QCommandLineOption createProjectOption(QStringLiteral("create-project"),
                                           QStringLiteral("Make and open a new project in this folder as the New project dialog does, "
                                                          "with --image as the plan image of floor 1. For checks only."),
                                           QStringLiteral("folder"));
    parser.addOption(createProjectOption);
    parser.addOption(newProjectDialogOption);
    parser.addOption(newProjectNameOption);
    parser.addOption(newProjectLocationOption);
    parser.addOption(floorMenuOption);
    parser.addOption(zoomOption);
    parser.addOption(overlayOption);
    parser.addOption(buildOption);
    parser.addOption(buildTabOption);
    parser.addOption(exportOption);
    parser.addOption(exportCheckOption);
    parser.addOption(exportTabOption);
    parser.addOption(exportGroupsExpandedOption);
    parser.addOption(viewOption);
    parser.addOption(cameraOption);
    parser.addOption(homeOption);
    QCommandLineOption helpPageOption(QStringLiteral("help-page"), QStringLiteral("Open the Help window at a page. For checks only."),
                                      QStringLiteral("id"));
    QCommandLineOption helpSearchOption(QStringLiteral("help-search"),
                                        QStringLiteral("Open the Help window with the hits of a search. For checks only."),
                                        QStringLiteral("text"));
    QCommandLineOption aboutOption(QStringLiteral("about"), QStringLiteral("Open About in the Help window. For checks only."));
    parser.addOption(helpPageOption);
    parser.addOption(helpSearchOption);
    parser.addOption(aboutOption);
    parser.addOption(integrationsTabOption);
    parser.addOption(blenderKitAddressOption);
    parser.addOption(fabImportOption);
    parser.addOption(integrationFileOption);
    parser.addOption(alignImageOption);
    parser.addOption(roofSectionsOption);
    parser.addOption(selectRoofOption);
    parser.addOption(noRoofsOption);
    parser.addOption(cutAtFloorOption);
    parser.addOption(furnitureAtOption);
    parser.addOption(hoverAssetOption);
    parser.addOption(assetCategoriesOption);
    parser.addOption(materialsTabOption);
    parser.addOption(selectMaterialOption);
    parser.addOption(materialPickerOption);
    parser.addOption(hoverMaterialOption);
    parser.addOption(pickMaterialOption);
    parser.addOption(setupOption);
    parser.addOption(setupItemOption);
    parser.addOption(importOption);
    parser.addOption(importMaterialsOption);
    parser.addOption(materialSourceOption);
    parser.addOption(addSamplesOption);
    QCommandLineOption addSampleGeneratorsOption(QStringLiteral("add-sample-generators"),
                                                 QStringLiteral("Copy the sample generators as the Catalog menu does. For checks only."));
    QCommandLineOption checkAddonsOption(QStringLiteral("check-addons"),
                                         QStringLiteral("Run the add-on check as the Catalog menu does, then wait for it. For checks only."));
    parser.addOption(addSampleGeneratorsOption);
    parser.addOption(checkAddonsOption);
    parser.addOption(addAssetMaterialOption);
    parser.addOption(catalogOption);
    parser.addOption(selectAssetOption);
    parser.addOption(toolOption);
    parser.addOption(selectOption);
    parser.addOption(wallCheckOption);
    parser.addOption(floorOption);
    QCommandLineOption generatorParamOption(QStringLiteral("generator-param"),
                                            QStringLiteral("Set a parameter of the selected generator item, for example diameter=0.5. "
                                                           "Repeat for several. For checks only."),
                                            QStringLiteral("id=value"));
    QCommandLineOption undoOption(QStringLiteral("undo"), QStringLiteral("Undo the last change once, after the other options. For checks only."));
    parser.addOption(generatorParamOption);
    parser.addOption(undoOption);
    parser.addOption(wallSideOption);
    parser.addOption(imageOption);
    parser.addOption(terminalOption);
    QCommandLineOption screenshotWindowOption(QStringLiteral("screenshot-window"),
                                              QStringLiteral("With --screenshot, open one of the app's own windows and save its picture instead: "
                                                             "assets, materials, sources, material-import, build or export."),
                                              QStringLiteral("name"));
    QCommandLineOption panelOption(QStringLiteral("panel"),
                                   QStringLiteral("Open or close the side panel, without saving it: open or closed. For checks only."),
                                   QStringLiteral("state"));
    QCommandLineOption updateStateOption(QStringLiteral("update-state"),
                                         QStringLiteral("With --screenshot, show a made up update state: ") +
                                             plynth::app::UpdateController::cannedNames().join(QStringLiteral(", ")) +
                                             QStringLiteral(". Nothing is sent, downloaded or installed. For checks only."),
                                         QStringLiteral("name"));
    parser.addOption(updateStateOption);
    QCommandLineOption windowSizeOption(QStringLiteral("window-size"),
                                        QStringLiteral("With --screenshot-window, the size of that window in pixels instead of its "
                                                       "default size, for example 1600x900."),
                                        QStringLiteral("WxH"));
    parser.addOption(windowSizeOption);
    parser.addOption(screenshotWindowOption);
    parser.addOption(panelOption);
    parser.addOption(screenshotOption);
    parser.addOption(sizeOption);
    if (!parser.parse(QApplication::arguments()))
        return kExitBadArguments;

    QSize size;
    const bool hasSize = parser.isSet(sizeOption);
    if (hasSize && !parseSize(parser.value(sizeOption), size))
        return kExitBadArguments;
    if (parser.isSet(toolOption)) {
        const QStringList names{QStringLiteral("select"), QStringLiteral("scale"), QStringLiteral("wall"),
                                QStringLiteral("opening"), QStringLiteral("room"), QStringLiteral("stair"),
                                QStringLiteral("furniture"), QStringLiteral("detect"), QStringLiteral("light")};
        if (!names.contains(parser.value(toolOption).toLower()))
            return kExitBadArguments;
    }
    if (parser.isSet(viewOption)) {
        const QString view = parser.value(viewOption).toLower();
        if (view != QStringLiteral("plan") && view != QStringLiteral("3d"))
            return kExitBadArguments;
    }
    if (parser.isSet(panelOption)) {
        const QString state = parser.value(panelOption).toLower();
        if (state != QStringLiteral("open") && state != QStringLiteral("closed"))
            return kExitBadArguments;
    }
    if (parser.isSet(cameraOption)) {
        const QString camera = parser.value(cameraOption).toLower();
        if (camera != QStringLiteral("three-quarter") && camera != QStringLiteral("top"))
            return kExitBadArguments;
    }
    if (parser.isSet(wallSideOption)) {
        const QString side = parser.value(wallSideOption).toLower();
        if (side != QStringLiteral("left") && side != QStringLiteral("right"))
            return kExitBadArguments;
    }
    double furnitureX = 0.0;
    double furnitureY = 0.0;
    struct LightPlace {
        double x = 0.0;
        double y = 0.0;
        QString kind;
    };
    QList<LightPlace> lightPlaces;
    for (const QString& value : parser.values(addLightOption)) {
        const QStringList parts = value.split(QLatin1Char(','));
        bool okX = false;
        bool okY = false;
        LightPlace place;
        if (parts.size() == 2 || parts.size() == 3) {
            place.x = parts[0].toDouble(&okX);
            place.y = parts[1].toDouble(&okY);
            place.kind = parts.size() == 3 ? parts[2].trimmed().toLower() : QStringLiteral("ceiling");
        }
        if (!okX || !okY || (place.kind != QStringLiteral("ceiling") && place.kind != QStringLiteral("wall") &&
                             place.kind != QStringLiteral("standing")))
            return kExitBadArguments;
        lightPlaces.append(place);
    }
    if (parser.isSet(furnitureAtOption)) {
        const QStringList parts = parser.value(furnitureAtOption).split(QLatin1Char(','));
        bool okX = false;
        bool okY = false;
        if (parts.size() == 2) {
            furnitureX = parts[0].toDouble(&okX);
            furnitureY = parts[1].toDouble(&okY);
        }
        if (!okX || !okY)
            return kExitBadArguments;
    }
    const bool wantsScreenshot = parser.isSet(screenshotOption);
    // A screenshot runs with no dialogs, no probe and nothing written.
    const bool quiet = wantsScreenshot;
    if (wantsScreenshot && parser.value(screenshotOption).isEmpty())
        return kExitBadArguments;
    if (parser.isSet(wallCheckOption) && !wantsScreenshot)
        return kExitBadArguments;
    const QString updateState = parser.value(updateStateOption).toLower();
    if (parser.isSet(updateStateOption) &&
        (!wantsScreenshot || !plynth::app::UpdateController::cannedNames().contains(updateState)))
        return kExitBadArguments;
    // Any option at all is a check or a screenshot. Only a plain start, with at most a project, checks for updates.
    const bool checkMode = !parser.optionNames().isEmpty();

    plynth::app::Theme& theme = plynth::app::Theme::instance();
    theme.load(QStringLiteral(":/themes/drafting-dark.json"));
    theme.loadBundledFonts();
    theme.apply();

    // A packaged program marks itself running, and refuses to start while its installer replaces it.
    const plynth::Status packaged = plynth::beginPackagedRun();
    if (!packaged.ok()) {
        if (!checkMode)
            showUpdateInProgress(packaged);
        return kExitUpdateInProgress;
    }

    QStringList positional = parser.positionalArguments();

    // Words right after the value of --terminal, up to the next option, belong to the text. They are
    // left over when a shell splits a quoted argument. They are never taken as the project folder.
    QStringList terminalWords;
    {
        const QStringList raw = QApplication::arguments();
        for (qsizetype i = 1; i < raw.size(); ++i) {
            const bool inline_ = raw.at(i).startsWith(QStringLiteral("--terminal="));
            if (raw.at(i) != QStringLiteral("--terminal") && !inline_)
                continue;
            qsizetype next = inline_ ? i + 1 : i + 2;
            for (; next < raw.size() && !raw.at(next).startsWith(QLatin1Char('-')); ++next)
                terminalWords.append(raw.at(next));
            break;
        }
        if (terminalWords.size() <= positional.size() &&
            positional.mid(positional.size() - terminalWords.size()) == terminalWords)
            positional = positional.mid(0, positional.size() - terminalWords.size());
        else
            terminalWords.clear();
    }
    // The file after the id of --integration-file is its second value, never the project folder.
    QString integrationFile;
    if (parser.isSet(integrationFileOption)) {
        const QStringList raw = QApplication::arguments();
        for (qsizetype i = 1; i < raw.size(); ++i) {
            const bool inline_ = raw.at(i).startsWith(QStringLiteral("--integration-file="));
            if (raw.at(i) != QStringLiteral("--integration-file") && !inline_)
                continue;
            const qsizetype next = inline_ ? i + 1 : i + 2;
            if (next < raw.size())
                integrationFile = raw.at(next);
            break;
        }
        const qsizetype at = positional.lastIndexOf(integrationFile);
        if (integrationFile.isEmpty() || at < 0)
            return kExitBadArguments;
        positional.removeAt(at);
    }
    if (quiet)
        plynth::app::LibraryHost::setAutoProbe(false);
    plynth::app::MainWindow window;
    window.setScreenshotMode(quiet);
    if (hasSize)
        window.resize(size);
    else
        window.resize(1280, 800);
    QString pictureWindow;
    if (parser.isSet(screenshotWindowOption)) {
        pictureWindow = parser.value(screenshotWindowOption).toLower();
        if (!window.toolWindowNames().contains(pictureWindow))
            return kExitBadArguments;
    }
    if (parser.isSet(windowSizeOption)) {
        QSize windowSize;
        if (!parseSize(parser.value(windowSizeOption), windowSize))
            return kExitBadArguments;
        window.setScreenshotSize(windowSize);
    }
    if (parser.isSet(panelOption))
        window.showSidePanel(parser.value(panelOption).toLower() == QStringLiteral("open"), false);

    QString loadError;
    QString loadTitle;
    bool loadFailed = false;
    const bool hasProject = !positional.isEmpty();
    if (hasProject) {
        loadFailed = !window.openProject(positional.first(), &loadError);
        loadTitle = QObject::tr("Cannot open project");
    }
    if (parser.isSet(createProjectOption) && !hasProject) {
        loadFailed = !window.createProjectForCheck(parser.value(createProjectOption), parser.value(imageOption));
        loadTitle = QObject::tr("Cannot create project");
        loadError = QObject::tr("The project could not be made in %1.").arg(parser.value(createProjectOption));
    } else if (parser.isSet(imageOption) && !(hasProject && loadFailed)) {
        QString imageError;
        const QString image = parser.value(imageOption);
        const bool ok = hasProject ? window.setPlanImage(image, &imageError)
                                   : window.newFromImage(image, &imageError);
        if (!ok && !loadFailed) {
            loadFailed = true;
            loadError = imageError;
            loadTitle = QObject::tr("Cannot open image");
        }
    }
    if (parser.isSet(floorOption))
        window.showFloor(parser.value(floorOption));
    if (parser.isSet(toolOption))
        window.activateToolByName(parser.value(toolOption));
    if (parser.isSet(lightToolOption))
        window.activateToolByName(QStringLiteral("light"));
    for (const LightPlace& place : lightPlaces)
        window.addLightForCheck(place.x, place.y, place.kind);
    if (parser.isSet(alignImageOption))
        window.setAlignImage(true);
    if (parser.isSet(roofSectionsOption))
        window.setRoofSectionsShown(true);
    if (parser.isSet(selectOption))
        window.selectById(parser.value(selectOption));
    if (parser.isSet(selectRoofOption))
        window.selectRoof(parser.value(selectRoofOption));
    if (parser.isSet(noRoofsOption))
        window.setShowRoofs3d(false);
    if (parser.isSet(cutAtFloorOption))
        window.setCutAtFloor(true);
    for (const QString& assignment : parser.values(generatorParamOption))
        window.setGeneratorParam(assignment);
    if (parser.isSet(undoOption))
        window.undoOnce();
    if (parser.isSet(wallSideOption))
        window.setWallViewSide(parser.value(wallSideOption).toLower() == QStringLiteral("left") ? plynth::WallSide::Left
                                                                                             : plynth::WallSide::Right);
    if (parser.isSet(viewOption))
        window.showView(parser.value(viewOption));
    if (parser.isSet(cameraOption))
        window.setPreviewCamera(parser.value(cameraOption));
    if (parser.isSet(catalogOption) || parser.isSet(selectAssetOption))
        window.showAssetsTab();
    if (parser.isSet(selectAssetOption)) {
        window.selectAsset(parser.value(selectAssetOption));
        if (parser.isSet(assetCategoriesOption))
            QTimer::singleShot(150, &window, [&window]() { window.showAssetCategories(); });
    }
    if (parser.isSet(materialsTabOption) || parser.isSet(selectMaterialOption))
        window.showMaterialsTab();
    if (parser.isSet(selectMaterialOption))
        window.selectMaterial(parser.value(selectMaterialOption));
    if (parser.isSet(homeOption) || (!quiet && !hasProject && !parser.isSet(imageOption) && !parser.isSet(createProjectOption)))
        window.showHome();
    window.show();
    if (parser.isSet(helpPageOption)) {
        const QString page = parser.value(helpPageOption);
        QTimer::singleShot(0, &window, [&window, page]() { window.guide()->openHelp(page); });
    } else if (parser.isSet(helpSearchOption)) {
        const QString text = parser.value(helpSearchOption);
        QTimer::singleShot(0, &window, [&window, text]() { window.guide()->openSearch(text); });
    } else if (parser.isSet(aboutOption)) {
        QTimer::singleShot(0, &window, [&window]() { window.guide()->openAbout(); });
    }
    if (parser.isSet(buildTabOption))
        window.showBuildTab();
    if (parser.isSet(exportGroupsExpandedOption))
        window.exportPanel()->setGroupsExpanded(true);
    if (parser.isSet(exportLicensesOnOption))
        window.exportPanel()->setLicensesEnabled(true);
    if (parser.isSet(exportTabOption) || parser.isSet(exportLicensesOnOption))
        window.showExportTab();
    if (parser.isSet(projectFinishOption))
        QTimer::singleShot(100, &window, [&window]() { window.showProjectFinish(); });
    if (parser.isSet(variationsDialogOption))
        QTimer::singleShot(0, &window, [&window]() { window.showVariationsDialog(false); });
    if (parser.isSet(newProjectDialogOption)) {
        const QString name = parser.value(newProjectNameOption);
        const QString location = parser.value(newProjectLocationOption);
        QTimer::singleShot(0, &window, [&window, name, location]() { window.showNewProjectDialogForCheck(name, location); });
    }
    if (parser.isSet(floorMenuOption)) {
        const QString floor = parser.value(floorMenuOption);
        QTimer::singleShot(250, &window, [&window, floor]() { window.showFloorMenuForCheck(floor); });
    }
    if (parser.isSet(openingAssetPickerOption))
        QTimer::singleShot(250, &window, [&window]() { window.showOpeningAssetPicker(QString()); });
    if (parser.isSet(blenderKitAddressOption))
        window.setBlenderKitAddress(parser.value(blenderKitAddressOption));
    else if (parser.isSet(integrationsTabOption))
        window.showIntegrationsTab();
    if (parser.isSet(fabImportOption)) {
        const QString zip = parser.value(fabImportOption);
        QTimer::singleShot(0, &window, [&window, zip]() { window.showFabImport(zip); });
    }
    if (parser.isSet(integrationFileOption)) {
        const QString id = parser.value(integrationFileOption);
        QTimer::singleShot(0, &window, [&window, id, integrationFile]() { window.importIntegrationFile(id, integrationFile); });
    }
    if (!pictureWindow.isEmpty()) {
        window.setPictureWindow(pictureWindow);
        window.openToolWindow(pictureWindow);
    }
    if (parser.isSet(overlayOption))
        window.setOverlay3d(true);
    if (parser.isSet(zoomOption) && parser.value(zoomOption).toLower() == QStringLiteral("actual"))
        QTimer::singleShot(100, &window, [&window]() { window.setActualSize(); });
    if (parser.isSet(buildOption))
        QTimer::singleShot(0, &window, [&window]() { window.startBuild(); });
    if (parser.isSet(swapBuildOption))
        window.swapBuildForCheck(parser.value(swapBuildOption));
    if (parser.isSet(flyCheckOption))
        window.flyForCheck(parser.value(flyCheckOption));
    if (parser.isSet(exportOption))
        QTimer::singleShot(0, &window, [&window]() { window.startExport(true, false); });
    else if (parser.isSet(exportCheckOption))
        QTimer::singleShot(0, &window, [&window]() { window.startExport(false, false); });
    if (parser.isSet(furnitureAtOption)) {
        const QString hover = parser.value(hoverAssetOption);
        QTimer::singleShot(250, &window, [&window, furnitureX, furnitureY, hover]() {
            window.showFurniturePicker(furnitureX, furnitureY, hover);
        });
    }
    if (parser.isSet(materialPickerOption)) {
        const QString target = parser.value(materialPickerOption);
        const QString hover = parser.value(hoverMaterialOption);
        const QString pick = parser.value(pickMaterialOption);
        QTimer::singleShot(250, &window, [&window, target, hover, pick]() { window.showMaterialPicker(target, hover, pick); });
    }
    if (parser.isSet(importOption))
        window.importFile(parser.value(importOption));
    if (parser.isSet(importMaterialsOption)) {
        for (const QString& source : parser.values(materialSourceOption))
            window.addMaterialSource(source);
        QTimer::singleShot(0, &window, [&window]() { window.showMaterialImport(); });
    }
    if (parser.isSet(addAssetMaterialOption)) {
        const QString both = parser.value(addAssetMaterialOption);
        QTimer::singleShot(0, &window, [&window, both]() {
            window.addAssetMaterialForCheck(both.section(QLatin1Char('/'), 0, 0), both.section(QLatin1Char('/'), 1));
        });
    }
    if (parser.isSet(addSampleGeneratorsOption))
        window.addSampleGenerators();
    if (parser.isSet(checkAddonsOption))
        window.checkAddonsAndWait();
    if (parser.isSet(addSamplesOption))
        QTimer::singleShot(0, &window, [&window]() { window.addSampleMaterials(); });
    if (parser.isSet(packDialogOption)) {
        const QString kind = parser.value(packDialogOption);
        QTimer::singleShot(0, &window, [&window, kind]() { window.showPackDialogForCheck(kind); });
    }
    if (parser.isSet(setupOption) || parser.isSet(setupItemOption)) {
        const QString item = parser.value(setupItemOption);
        QTimer::singleShot(0, &window, [&window, item]() { window.showSetupCheck(item); });
    }
    else if (!quiet)
        QTimer::singleShot(0, &window, [&window]() { window.openSetupCheckIfNeeded(); });
    // After the setup check had its turn, so a setup check that opened is waited for.
    if (!quiet && !checkMode)
        QTimer::singleShot(0, &window, [&window]() { window.startUpdateCheck(); });
    if (!updateState.isEmpty())
        QTimer::singleShot(0, &window, [&window, updateState]() { window.showUpdateStateForCheck(updateState); });
    if (loadFailed && !quiet)
        QTimer::singleShot(0, &window, [&window, loadTitle, loadError]() { window.showError(loadTitle, loadError); });
    else if (hasProject && !loadFailed && !quiet)
        QTimer::singleShot(0, &window, [&window]() { window.offerRecovery(); });

    const bool wantsTerminal = parser.isSet(terminalOption);
    if (wantsTerminal) {
        // The words found right after the --terminal value, up to the next option, are the rest of
        // the text, in case the shell that started the app split a quoted argument into several.
        QString text = parser.value(terminalOption);
        for (const QString& word : terminalWords)
            text += QLatin1Char(' ') + word;
        QTimer::singleShot(0, &window, [&window, text]() { window.startTerminalCheck(text); });
    }

    const bool wantsBuild = parser.isSet(buildOption) || parser.isSet(exportOption) || parser.isSet(exportCheckOption);
    auto wallCheckDone = std::make_shared<bool>(!parser.isSet(wallCheckOption));
    if (parser.isSet(wallCheckOption)) {
        const QString spec = parser.value(wallCheckOption);
        const QString file = parser.value(screenshotOption);
        const QString stem = file.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive) ? file.chopped(4) : file;
        QTimer::singleShot(500, &window, [&window, spec, file, stem, wallCheckDone]() {
            window.runWallCheck(spec, file + QStringLiteral(".txt"), stem + QStringLiteral("-middle.png"));
            *wallCheckDone = true;
        });
    }
    if (wantsScreenshot) {
        const QString file = parser.value(screenshotOption);
        // The main window is saved at --size. One of the app's own windows is saved at its own size, so the
        // picture shows what a user sees when it opens.
        const QSize target = !pictureWindow.isEmpty() ? QSize() : hasSize ? size : window.size();
        QElapsedTimer waited;
        waited.start();
        auto* poll = new QTimer(&app);
        poll->setInterval(wantsTerminal ? 100 : 400);
        QObject::connect(poll, &QTimer::timeout, &app, [&window, file, target, loadFailed, wantsTerminal, wantsBuild, poll, waited, wallCheckDone]() {
            if (wantsTerminal && !window.terminalPictureReady())
                return;
            if (!*wallCheckDone)
                return;
            if (!window.detectionPictureReady() && !waited.hasExpired(kDetectionWaitMs))
                return;
            if (!window.previewPictureReady() && !waited.hasExpired(wantsBuild ? kBuildWaitMs : kPreviewWaitMs))
                return;
            if ((!window.setupPictureReady() || !window.importPictureReady()) && !waited.hasExpired(kSetupWaitMs))
                return;
            if (!window.materialsPictureReady() && !waited.hasExpired(kBuildWaitMs))
                return;
            if (!window.integrationsPictureReady() && !waited.hasExpired(kSetupWaitMs))
                return;
            poll->stop();
            // Panels rebuilt just before need a few passes to show and lay out their rows.
            for (int pass = 0; pass < 4; ++pass)
                QApplication::processEvents();
            const bool ok = saveScreenshot(window, file, target);
            const bool problem = loadFailed || window.planImageFailed();
            QApplication::exit(!ok                    ? kExitSaveFailed
                               : window.terminalFailed() ? kExitTerminalFailed
                               : problem                 ? kExitImageFailed
                                                         : kExitSaved);
        });
        poll->start();
    }
    return app.exec();
}
