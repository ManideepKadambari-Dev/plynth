#pragma once

#include <QKeySequence>
#include <QList>
#include <QMainWindow>

class QAction;
class QDragEnterEvent;
class QDropEvent;

namespace plynth::app {

class TitleBar;
class PlanCanvas;
class StatusBar;

// The main application window with its own title bar.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString& projectFolder, QWidget* parent = nullptr);

    QString projectName() const { return projectName_; }
    TitleBar* titleBar() const { return titleBar_; }

    bool loadImage(const QString& path, QString* error);
    void showImageError(const QString& message);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void toggleMaximized();
    void showAbout();
    void openImageDialog();
    void updateViewActions();
    QAction* addViewAction(const QString& text, const QString& tip, const QString& tourId,
                           const QList<QKeySequence>& keys, void (PlanCanvas::*slot)());

    QString projectName_;
    TitleBar* titleBar_ = nullptr;
    PlanCanvas* canvas_ = nullptr;
    StatusBar* statusBar_ = nullptr;
    QList<QAction*> viewActions_;
    QString lastFolder_;
    bool nativeFrameReady_ = false;
};

}
