#pragma once

#include <plynth/core/Settings.h>

#include <QSize>
#include <QString>
#include <QWidget>

#include <optional>

class QTimer;

namespace plynth::app {

class TitleBar;

// One of the app's own windows, such as Assets or Export: the frameless title bar of the main window without
// menus, over one panel. There is one window of each kind; opening it again brings it to the front. It keeps
// no state of its own beyond its place on the desktop, which it reports so the settings can remember it.
class ToolWindow : public QWidget {
    Q_OBJECT
public:
    // A column width above 0 keeps the content in a centred column at most that wide, for pages of text and
    // buttons that would look lost across a wide window. Default size is the size it first opens at.
    ToolWindow(const QString& name, const QString& title, QWidget* content, const QSize& defaultSize, int columnWidth = 0);

    // The name the settings use for it, such as "assets".
    QString name() const { return name_; }
    QWidget* content() const { return content_; }
    QSize defaultSize() const { return defaultSize_; }

    // The place to use the first time it opens: a remembered one, or none to centre it over the anchor window.
    // Step moves a new window down and right a little, so windows opened for the first time do not hide
    // each other.
    void setStartPlace(const std::optional<plynth::WindowPlace>& place, QWidget* anchor, int step);
    // Shows the window at its start place the first time, and brings it to the front.
    void open();
    // Where the window is now, with its size before a maximize.
    plynth::WindowPlace place() const;
    // Off in screenshot mode: the window then never reports a new place.
    void setReportsPlace(bool on) { reportsPlace_ = on; }

signals:
    // Sent a moment after the window was moved, resized, maximized or restored.
    void placeChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void applyStartPlace();
    void toggleMaximized();
    void placeMoved();

    QString name_;
    QWidget* content_ = nullptr;
    QSize defaultSize_;
    TitleBar* titleBar_ = nullptr;
    QTimer* placeTimer_ = nullptr;
    std::optional<plynth::WindowPlace> startPlace_;
    QWidget* anchor_ = nullptr;
    int step_ = 0;
    bool placed_ = false;
    bool reportsPlace_ = true;
    bool nativeFrameReady_ = false;
};

}
