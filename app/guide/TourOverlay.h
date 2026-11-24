#pragma once

#include "GuideStyle.h"

#include <QPointer>
#include <QWidget>

namespace plynth::guide {

class TourCard;

// The layer a tour draws over the window: it dims everything, outlines one control in amber and shows the
// card next to it. It takes every click and key, so nothing in the app runs while a tour is shown.
class TourOverlay : public QWidget {
    Q_OBJECT
public:
    TourOverlay(const GuideStyle& style, QWidget* window);

    TourCard* card() const { return card_; }

    // Outlines this control and moves the card next to it. Null shows the card with no outline.
    void setTarget(QWidget* control);

    // Lays the layer over the whole window again and moves the card.
    void updateLayout();

signals:
    void backRequested();
    void nextRequested();
    void skipRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // The outlined area in this widget's coordinates. Empty when there is no target.
    QRect targetRect() const;

    GuideStyle style_;
    TourCard* card_ = nullptr;
    QPointer<QWidget> target_;
};

}
