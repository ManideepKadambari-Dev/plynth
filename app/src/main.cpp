#include "panels/ExportPanel.h"
#include "theme/Theme.h"
#include "window/MainWindow.h"

#include <plynth/cli/Cli.h>
#include <plynth/core/Version.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QImage>
#include <QRegularExpression>
#include <QTimer>

namespace {

constexpr int kExitSaved = 0;
constexpr int kExitSaveFailed = 1;
constexpr int kExitBadArguments = 2;
constexpr int kExitImageFailed = 3;
constexpr int kExitTerminalFailed = 4;
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

// Saves a picture of the window at exactly the requested pixel size.
bool saveScreenshot(plynth::app::MainWindow& window, const QString& file, const QSize& size)
{
    QImage image = window.grabPicture();
    if (image.size() != size)
        image = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    image.setDevicePixelRatio(1.0);
    return image.save(file, "PNG");
}

}

// Parses the options, opens the window and optionally saves a screenshot.
int main(int argc, char* argv[])
{
    if (argc > 1 && plynth::cli::isCommandWord(argv[1]))
        return plynth::cli::runFromProcess(argc, argv);

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
                                  QStringLiteral("Activate a tool: select, scale, wall, opening, room, furniture or detect. For checks only."),
                                  QStringLiteral("name"));
    QCommandLineOption selectOption(QStringLiteral("select"),
                                    QStringLiteral("Select the point, wall, opening or room with this id. For checks only."),
                                    QStringLiteral("id"));
    QCommandLineOption wallSideOption(QStringLiteral("wall-side"),
                                      QStringLiteral("Side of the wall the wall view is seen from: left or right. For checks only."),
                                      QStringLiteral("side"));
    QCommandLineOption catalogOption(QStringLiteral("catalog"),
                                     QStringLiteral("Show the Assets tab. For checks only."));
    QCommandLineOption selectAssetOption(QStringLiteral("select-asset"),
                                         QStringLiteral("Select the asset with this id in the Assets tab. For checks only."),
                                         QStringLiteral("id"));
    QCommandLineOption assetCategoriesOption(QStringLiteral("asset-categories"),
                                             QStringLiteral("With --select-asset, scroll the details to the categories. For checks only."));
    QCommandLineOption materialsTabOption(QStringLiteral("materials-tab"),
                                          QStringLiteral("Show the Materials tab. For checks only."));
    QCommandLineOption selectMaterialOption(QStringLiteral("select-material"),
                                            QStringLiteral("Select the material with this id in the Materials tab. For checks only."),
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
                                             QStringLiteral("Show the material import screen. For checks only."));
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
    QCommandLineOption buildTabOption(QStringLiteral("build-tab"), QStringLiteral("Show the Build tab. For checks only."));
    QCommandLineOption exportOption(QStringLiteral("export"),
                                    QStringLiteral("Export the last build after opening the project and wait for the end. For checks only."));
    QCommandLineOption exportCheckOption(QStringLiteral("export-check"),
                                         QStringLiteral("Run the export quality checks after opening the project and wait for the end. For checks only."));
    QCommandLineOption exportTabOption(QStringLiteral("export-tab"), QStringLiteral("Show the Export tab. For checks only."));
    QCommandLineOption exportGroupsExpandedOption(QStringLiteral("export-groups-expanded"),
                                                  QStringLiteral("Open every group in the Export tab. For checks only."));
    QCommandLineOption homeOption(QStringLiteral("home"), QStringLiteral("Show the home screen. For checks only."));
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
    parser.addOption(addAssetMaterialOption);
    parser.addOption(catalogOption);
    parser.addOption(selectAssetOption);
    parser.addOption(toolOption);
    parser.addOption(selectOption);
    parser.addOption(wallSideOption);
    parser.addOption(imageOption);
    parser.addOption(terminalOption);
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
                                QStringLiteral("opening"), QStringLiteral("room"), QStringLiteral("furniture"),
                                QStringLiteral("detect")};
        if (!names.contains(parser.value(toolOption).toLower()))
            return kExitBadArguments;
    }
    if (parser.isSet(viewOption)) {
        const QString view = parser.value(viewOption).toLower();
        if (view != QStringLiteral("plan") && view != QStringLiteral("3d"))
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
    if (wantsScreenshot && parser.value(screenshotOption).isEmpty())
        return kExitBadArguments;

    plynth::app::Theme& theme = plynth::app::Theme::instance();
    theme.load(QStringLiteral(":/themes/drafting-dark.json"));
    theme.loadBundledFonts();
    theme.apply();

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
    plynth::app::MainWindow window;
    window.setScreenshotMode(wantsScreenshot);
    if (hasSize)
        window.resize(size);
    else
        window.resize(1280, 800);

    QString loadError;
    QString loadTitle;
    bool loadFailed = false;
    const bool hasProject = !positional.isEmpty();
    if (hasProject) {
        loadFailed = !window.openProject(positional.first(), &loadError);
        loadTitle = QObject::tr("Cannot open project");
    }
    if (parser.isSet(imageOption) && !(hasProject && loadFailed)) {
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
    if (parser.isSet(toolOption))
        window.activateToolByName(parser.value(toolOption));
    if (parser.isSet(selectOption))
        window.selectById(parser.value(selectOption));
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
    if (parser.isSet(homeOption) || (!wantsScreenshot && !hasProject && !parser.isSet(imageOption)))
        window.showHome();
    window.show();
    if (parser.isSet(buildTabOption))
        window.showBuildTab();
    if (parser.isSet(exportGroupsExpandedOption))
        window.findChild<plynth::app::ExportPanel*>()->setGroupsExpanded(true);
    if (parser.isSet(exportTabOption))
        window.showExportTab();
    if (parser.isSet(overlayOption))
        window.setOverlay3d(true);
    if (parser.isSet(zoomOption) && parser.value(zoomOption).toLower() == QStringLiteral("actual"))
        QTimer::singleShot(100, &window, [&window]() { window.setActualSize(); });
    if (parser.isSet(buildOption))
        QTimer::singleShot(0, &window, [&window]() { window.startBuild(); });
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
    if (parser.isSet(addSamplesOption))
        QTimer::singleShot(0, &window, [&window]() { window.addSampleMaterials(); });
    if (parser.isSet(setupOption) || parser.isSet(setupItemOption)) {
        const QString item = parser.value(setupItemOption);
        QTimer::singleShot(0, &window, [&window, item]() { window.showSetupCheck(item); });
    }
    else if (!wantsScreenshot)
        QTimer::singleShot(0, &window, [&window]() { window.openSetupCheckIfNeeded(); });
    if (loadFailed && !wantsScreenshot)
        QTimer::singleShot(0, &window, [&window, loadTitle, loadError]() { window.showError(loadTitle, loadError); });
    else if (hasProject && !loadFailed && !wantsScreenshot)
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
    if (wantsScreenshot) {
        const QString file = parser.value(screenshotOption);
        const QSize target = hasSize ? size : window.size();
        QElapsedTimer waited;
        waited.start();
        auto* poll = new QTimer(&app);
        poll->setInterval(wantsTerminal ? 100 : 400);
        QObject::connect(poll, &QTimer::timeout, &app, [&window, file, target, loadFailed, wantsTerminal, wantsBuild, poll, waited]() {
            if (wantsTerminal && !window.terminalPictureReady())
                return;
            if (!window.detectionPictureReady() && !waited.hasExpired(kDetectionWaitMs))
                return;
            if (!window.previewPictureReady() && !waited.hasExpired(wantsBuild ? kBuildWaitMs : kPreviewWaitMs))
                return;
            if ((!window.setupPictureReady() || !window.importPictureReady()) && !waited.hasExpired(kSetupWaitMs))
                return;
            if (!window.materialsPictureReady() && !waited.hasExpired(kBuildWaitMs))
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
