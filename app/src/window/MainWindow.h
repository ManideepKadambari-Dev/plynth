#pragma once

#include "tools/Tool.h"

#include <plynth/core/Build.h>
#include <plynth/core/Document.h>
#include <plynth/core/EditSession.h>
#include <plynth/core/Export.h>
#include <plynth/core/ProjectFiles.h>

#include <QImage>
#include <QKeySequence>
#include <QList>
#include <QMainWindow>
#include <QPointer>

#include <memory>
#include <optional>

class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QMenu;
class QSplitter;
class QStackedWidget;
class QTimer;

namespace plynth::app {

class AssetsPanel;
class MaterialImportQueue;
class MaterialImportScreen;
class MaterialRefresher;
class MaterialsPanel;
class BuildBar;
class BuildController;
class BuildPanel;
class DocumentBridge;
class FloorFocus;
class FloorTabs;
class ExportController;
class ExportPanel;
class HomeScreen;
class ImportQueue;
class ImportQueueView;
class LibraryHost;
class LibraryWatcher;
class SetupCheckDialog;
class ProjectWatcher;
class TerminalDock;
class ReadOnlyBanner;
class TitleBar;
class PlanCanvas;
class OverlayRenderer;
class PreviewView;
class PropertiesPanel;
class RightPanel;
class Selection;
class StatusBar;
class ToolController;
class ToolStrip;
class WallView;

// The main application window with its own title bar. It owns the one document of the app and
// runs every change through it.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    QString projectName() const;
    TitleBar* titleBar() const { return titleBar_; }

    // Screenshot mode: no dialogs, no autosave, nothing written to the project folder.
    void setScreenshotMode(bool on);

    // Open a project folder, replacing the document. No questions asked. On failure the document is unchanged.
    bool openProject(const QString& folder, QString* error);
    // Starts an untitled project whose floor has this image. No undo step.
    bool newFromImage(const QString& path, QString* error);
    // Sets the image of the shown floor with an undoable command.
    bool setPlanImage(const QString& path, QString* error);
    // True when the shown floor has an image that could not be opened.
    bool planImageFailed() const { return imageFailed_; }
    // Asks whether to recover the autosave of the open project, when there is one.
    void offerRecovery();

    void showError(const QString& title, const QString& message);

    // For checks only: activates a tool by name (select, scale, wall, room, detect). False for an unknown name.
    bool activateToolByName(const QString& name);
    // For checks only: shows the floor with this id. False for an unknown id.
    bool showFloor(const QString& id);
    // For checks only: selects the point, wall or room with this id, switching to its floor first. An unknown id selects nothing.
    void selectById(const QString& id);
    // For checks only: turns image alignment of the shown floor on or off, as View > Align plan image.
    void setAlignImage(bool on);
    // For checks only: turns the roof sections of the shown floor on the plan on or off, as View > Roof sections.
    void setRoofSectionsShown(bool on);
    // For checks only: shows the floor of a roof section ("<floor id>:<index>"), turns roof sections on and selects it.
    void selectRoof(const QString& id);
    // For checks only: View > Show roofs in 3D and View > Cut 3D at the shown floor.
    void setShowRoofs3d(bool on);
    void setCutAtFloor(bool on);
    bool showRoofs3d() const;
    bool cutAtFloor() const;
    // For checks only: sets a parameter of the selected generator item ("diameter=0.5"). False when refused.
    bool setGeneratorParam(const QString& assignment);
    // For checks only: the Catalog menu's Add the sample generators.
    void addSampleGenerators();
    // For checks only: runs the add-on check and returns when it has finished.
    void checkAddonsAndWait();
    // For checks only: undoes the last change once.
    void undoOnce() { doc_.undo(); }
    // For checks only: chooses the side the wall view is seen from.
    void setWallViewSide(plynth::WallSide side);

    // Picture of the window for screenshots: the setup check, when open, is drawn over a dimmed window.
    QImage grabPicture();
    // For checks only: opens the setup check and keeps it open.
    void showSetupCheck(const QString& scrollToTitle = QString());
    // For checks only: true when no setup check is open, or its check has finished.
    bool setupPictureReady();
    // For checks only: queues an import of a file or folder.
    void importFile(const QString& path);
    // For checks only: true when no import was asked for, or its progress row is showing.
    bool importPictureReady() const;
    // Opens the setup check at start when this is the first run, after an update, or a required item blocks.
    void openSetupCheckIfNeeded();

    // For checks only: opens the furniture picker at a place on the plan (metres) and hovers an asset when
    // an id is given. False when there is no scale or no library.
    bool showFurniturePicker(double x, double y, const QString& hoverAsset);

    // Shows the home screen over the work area. A project that is open stays open behind it.
    void showHome();
    // Shows the work area (plan, panels) again.
    void showWork();
    // Closes the project after the save question and shows the home screen.
    void closeProject();

    // For checks only: shows the Assets tab.
    void showAssetsTab();
    // For checks only: selects an asset in the Assets tab. False when it is not listed.
    bool selectAsset(const QString& id);
    // For checks only: scrolls the asset details to the category list.
    void showAssetCategories();

    // Shows the material import screen over the work area.
    void showMaterialImport();
    // Queues every folder of samples/materials, skipping ids already in the library.
    void addSampleMaterials();
    // For checks only: takes a material of an asset into the materials catalog, as its button does.
    void addAssetMaterialForCheck(const QString& assetId, const QString& materialName) { addMaterialFromAsset(assetId, materialName); }
    // For checks only: adds a path to the import screen list.
    void addMaterialSource(const QString& path);
    // For checks only: true when no material import was asked for, or the queue and the renders are done.
    bool materialsPictureReady() const;
    // For checks only: shows the Materials tab.
    void showMaterialsTab();
    // For checks only: selects a material in the Materials tab. False when it is not listed.
    bool selectMaterial(const QString& id);
    // For checks only: selects the wall or room a material target such as "w3.outside" names (nothing for a
    // default such as "default.floor"), shows Properties and opens its material picker, hovering a material
    // when an id is given. False when the target is unknown.
    // With pickMaterial, chooses that material at once, as a click would.
    bool showMaterialPicker(const QString& target, const QString& hoverMaterial, const QString& pickMaterial = QString());

    // For checks only: opens the terminal and types the text once the shell has printed something.
    void startTerminalCheck(const QString& text);
    bool terminalPictureReady() const;
    bool terminalFailed() const;
    // Starts a build of the project in memory. Asks to save a project that was never saved.
    void startBuild();
    // For checks only: shows the plan at 100 percent.
    void setActualSize();
    // For checks only: turns the 3D overlay on the plan on or off.
    void setOverlay3d(bool on);
    // For checks only: shows the Build tab.
    void showBuildTab();
    // Starts an export of the last build, or only its quality checks when write is false. Force goes past failed checks.
    void startExport(bool write, bool force);
    // For checks only: shows the Export tab.
    void showExportTab();
    // For checks only: shows the 3D page ("3d") or the plan ("plan").
    void showView(const QString& name);
    // For checks only: sets the camera of the 3D page: "three-quarter" or "top".
    void setPreviewCamera(const QString& name);
    // For checks only: true when the 3D page has drawn its model, or is not shown, or has none.
    bool previewPictureReady() const;
    // For checks only: true when the Detect tool is off, or its first detection has finished or failed.
    bool detectionPictureReady() const;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void toggleMaximized();
    void recordRecent();
    void openRecent(const QString& folder);
    void showAbout();
    void buildCatalogMenu();
    void openSetupCheck();
    void importDialog();
    void importPaths(const QStringList& paths);
    void materialImportFinished(bool ok, const QString& materialId, const QString& message, bool skipped, bool thumbnailFailed);
    void addMaterialFromAsset(const QString& assetId, const QString& materialName);
    void queueFinished(int kind, bool ok, const QString& assetId, const QString& message, bool thumbnailFailed);
    void editRoomTypes();
    void openLibraryFolder();
    void openGeneratorsFolder();
    void roomTypesChanged();
    void reportMissingImages(const plynth::SaveReport& report);
    void buildMenus();
    void newProject();
    void newProjectFromImage();
    void openProjectDialog();
    void setImageDialog();
    bool saveDocument();
    bool saveDocumentAs();
    bool confirmDiscardChanges();
    void onDocumentChanged(unsigned changes);
    void updateTitle();
    void updateActions();
    void updateViewActions();
    void refreshImage();
    void refreshFloorTabs();
    void floorShown();
    void addFloor(bool above, const QString& besideFloor);
    void renameFloor(const QString& floorId);
    void removeFloor(const QString& floorId);
    void stepFloor(int direction);
    void updateAlignAction();
    void refreshOverlay();
    void refreshPreview();
    void applyPreviewRoofOptions();
    void refreshProblemMarkers();
    void refreshBuilding3d();
    void buildFinished(const plynth::BuildReport& report);
    void updateBuildUi();
    void loadBuildReport();
    void showProblem(const QString& objectId);
    void openBuildFolder();
    void openInBlender();
    void save3dPicture();
    bool buildBlocked(QString* reason) const;
    QString exportNotice(bool* offerBuild) const;
    void exportFinished(const plynth::ExportReport& report);
    void exportAnyway();
    void loadExportReport();
    void updateExportUi();
    void openExportFolder();
    void cancelWork();
    void showCentreView(bool scene3d);
    void refreshSelection();
    void deleteSelection();
    void updateDeleteAction();
    void updateWallView();
    void sizeWallView();
    void validateSelection();
    void updateToolActions();
    void updateCursorText(const QPointF* scene);
    void syncWatcher();
    void handleCheck();
    bool reloadFromDisk();
    void reloadOrAsk();
    void updateReadOnlyUi();
    void unlockNow();
    bool decodeImage(const QString& path, QImage* image, QString* error);
    void cacheImage(const QString& path, const QImage& image);
    void syncAppState();
    void removeAppState();
    void runAutosave();
    QString imageFilter() const;
    QAction* addMenuAction(QMenu* menu, const QString& text, const QString& tip, const QString& tourId,
                           const QList<QKeySequence>& keys);
    QAction* addViewAction(const QString& text, const QString& tip, const QString& tourId,
                           const QList<QKeySequence>& keys, void (PlanCanvas::*slot)());

    plynth::Document doc_;
    std::unique_ptr<DocumentBridge> bridge_;
    plynth::ProcessIdentity self_;

    TitleBar* titleBar_ = nullptr;
    FloorFocus* focus_ = nullptr;
    FloorTabs* floorTabs_ = nullptr;
    PlanCanvas* canvas_ = nullptr;
    Selection* selection_ = nullptr;
    ToolController* tools_ = nullptr;
    ToolStrip* toolStrip_ = nullptr;
    PropertiesPanel* panel_ = nullptr;
    AssetsPanel* assets_ = nullptr;
    MaterialsPanel* materials_ = nullptr;
    MaterialRefresher* materialRefresher_ = nullptr;
    MaterialImportQueue* materialQueue_ = nullptr;
    MaterialImportScreen* materialImport_ = nullptr;
    bool materialImportAsked_ = false;
    RightPanel* right_ = nullptr;
    LibraryHost* host_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QStackedWidget* centreStack_ = nullptr;
    BuildBar* buildBar_ = nullptr;
    PreviewView* preview_ = nullptr;
    QAction* view3dAction_ = nullptr;
    BuildController* builder_ = nullptr;
    OverlayRenderer* overlayRenderer_ = nullptr;
    QAction* overlay3dAction_ = nullptr;
    QAction* roofSectionsAction_ = nullptr;
    QAction* showRoofs3dAction_ = nullptr;
    QAction* cutAtFloorAction_ = nullptr;
    QAction* problemsOnPlanAction_ = nullptr;
    BuildPanel* buildPanel_ = nullptr;
    ExportController* exporter_ = nullptr;
    ExportPanel* exportPanel_ = nullptr;
    QAction* exportAction_ = nullptr;
    QAction* checkExportAction_ = nullptr;
    QAction* exportAnywayAction_ = nullptr;
    QAction* exportFolderAction_ = nullptr;
    std::optional<plynth::ExportReport> exportReport_;
    QString exportError_;
    QAction* buildAction_ = nullptr;
    QAction* cancelBuildAction_ = nullptr;
    QAction* reportAction_ = nullptr;
    QAction* buildFolderAction_ = nullptr;
    QAction* openBlenderAction_ = nullptr;
    QAction* save3dPictureAction_ = nullptr;
    std::optional<plynth::BuildReport> report_;
    QTimer* buildUiTimer_ = nullptr;
    QString buildProgressText_;
    int buildProgressPercent_ = 0;
    QAction* fitAction_ = nullptr;
    QWidget* work_ = nullptr;
    HomeScreen* home_ = nullptr;
    QAction* homeAction_ = nullptr;
    QAction* closeAction_ = nullptr;
    QAction* importAction_ = nullptr;
    QAction* rescanAction_ = nullptr;
    QAction* rescanMaterialsAction_ = nullptr;
    QAction* importMaterialsAction_ = nullptr;
    QAction* addSamplesAction_ = nullptr;
    QAction* addShapesAction_ = nullptr;
    QAction* reloadGeneratorsAction_ = nullptr;
    QAction* checkAddonsAction_ = nullptr;
    QAction* addSampleGeneratorsAction_ = nullptr;
    QAction* openGeneratorsAction_ = nullptr;
    ImportQueue* queue_ = nullptr;
    ImportQueueView* queueView_ = nullptr;
    LibraryWatcher* libraryWatcher_ = nullptr;
    QPointer<SetupCheckDialog> setupDialog_;
    bool importAsked_ = false;
    QString setupScrollTo_;
    QSplitter* splitter_ = nullptr;
    WallView* wallView_ = nullptr;
    QAction* wallViewAction_ = nullptr;
    QAction* flipWallViewAction_ = nullptr;
    bool wallViewSized_ = false;
    QAction* toolActions_[kToolCount] = {};
    QAction* alignImageAction_ = nullptr;
    QAction* floorUpAction_ = nullptr;
    QAction* floorDownAction_ = nullptr;
    QAction* acceptDetectionAction_ = nullptr;
    QAction* turnLeftAction_ = nullptr;
    QAction* turnRightAction_ = nullptr;
    QAction* mountAction_ = nullptr;
    QAction* takeOffAction_ = nullptr;
    QAction* swapAction_ = nullptr;
    QAction* updateSizeAction_ = nullptr;
    QAction* snapGridAction_ = nullptr;
    QAction* deleteAction_ = nullptr;
    StatusBar* statusBar_ = nullptr;
    QTimer* autosaveTimer_ = nullptr;
    ReadOnlyBanner* banner_ = nullptr;
    ProjectWatcher* watcher_ = nullptr;
    TerminalDock* terminal_ = nullptr;
    QList<QAction*> viewActions_;
    QAction* newAction_ = nullptr;
    QAction* newFromImageAction_ = nullptr;
    QAction* openAction_ = nullptr;
    QAction* setImageAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* saveAsAction_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;

    QString lastFolder_;
    QString lastAssetFolder_;
    QString shownImageKey_;
    QString cachedImageKey_;
    QImage cachedImage_;
    bool imageFailed_ = false;
    bool screenshotMode_ = false;
    bool recoveryPending_ = false;
    bool reloadPending_ = false;
    bool asking_ = false;
    bool nativeFrameReady_ = false;

    std::filesystem::path stateFolder_;
    std::optional<bool> stateModified_;
};

}
