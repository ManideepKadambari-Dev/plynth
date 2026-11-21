#pragma once

#include "tools/Tool.h"

#include <plynth/core/Document.h>
#include <plynth/core/EditSession.h>
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
class DocumentBridge;
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
    // For checks only: selects the point, wall or room with this id on the first floor. An unknown id selects nothing.
    void selectById(const QString& id);
    // For checks only: chooses the side the wall view is seen from.
    void setWallViewSide(plynth::WallSide side);

    // Picture of the window for screenshots: the setup check, when open, is drawn over a dimmed window.
    QImage grabPicture();
    // For checks only: opens the setup check and keeps it open.
    void showSetupCheck();
    // For checks only: true when no setup check is open, or its check has finished.
    bool setupPictureReady() const;
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

    // For checks only: opens the terminal and types the text once the shell has printed something.
    void startTerminalCheck(const QString& text);
    bool terminalPictureReady() const;
    bool terminalFailed() const;
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
    void queueFinished(int kind, bool ok, const QString& assetId, const QString& message, bool thumbnailFailed);
    void editRoomTypes();
    void openLibraryFolder();
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
    void refreshOverlay();
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
    PlanCanvas* canvas_ = nullptr;
    Selection* selection_ = nullptr;
    ToolController* tools_ = nullptr;
    ToolStrip* toolStrip_ = nullptr;
    PropertiesPanel* panel_ = nullptr;
    AssetsPanel* assets_ = nullptr;
    RightPanel* right_ = nullptr;
    LibraryHost* host_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QWidget* work_ = nullptr;
    HomeScreen* home_ = nullptr;
    QAction* homeAction_ = nullptr;
    QAction* closeAction_ = nullptr;
    QAction* importAction_ = nullptr;
    QAction* rescanAction_ = nullptr;
    ImportQueue* queue_ = nullptr;
    ImportQueueView* queueView_ = nullptr;
    LibraryWatcher* libraryWatcher_ = nullptr;
    QPointer<SetupCheckDialog> setupDialog_;
    bool importAsked_ = false;
    QSplitter* splitter_ = nullptr;
    WallView* wallView_ = nullptr;
    QAction* wallViewAction_ = nullptr;
    QAction* flipWallViewAction_ = nullptr;
    bool wallViewSized_ = false;
    QAction* toolActions_[kAllTools.size()] = {};
    QAction* acceptDetectionAction_ = nullptr;
    QAction* turnLeftAction_ = nullptr;
    QAction* turnRightAction_ = nullptr;
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
