#pragma once

#include <QWidget>

class QLabel;
class QTimer;

namespace plynth::app {

// A thin bar at the bottom of the window: image name and size on the left, zoom on the right.
class StatusBar : public QWidget {
    Q_OBJECT
public:
    explicit StatusBar(QWidget* parent = nullptr);

    void setImageInfo(const QString& fileName, int width, int height);
    void clearImageInfo();
    void setZoom(double zoom);
    void clearZoom();
    // The cursor position text, for example "x 3.250 m  y 1.500 m". Empty hides it.
    void setCursorText(const QString& text);
    // The one line hint for the active tool.
    void setHint(const QString& text);
    // A refused command, shown in amber for a few seconds in place of the hint.
    void showMessage(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* nameLabel_ = nullptr;
    QLabel* sizeLabel_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
    QLabel* hintLabel_ = nullptr;
    QLabel* messageLabel_ = nullptr;
    QLabel* cursorLabel_ = nullptr;
    QTimer* messageTimer_ = nullptr;
};

}
