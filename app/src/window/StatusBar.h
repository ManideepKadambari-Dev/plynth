#pragma once

#include <QWidget>

class QLabel;

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

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* nameLabel_ = nullptr;
    QLabel* sizeLabel_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
};

}
