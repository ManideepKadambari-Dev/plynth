#pragma once

#include "wallview/WallViewData.h"

#include <QGraphicsView>
#include <QPoint>
#include <QPointF>

namespace plynth::app {

// The drawing area of the wall view: one wall face at true scale, the same size across and up. It paints from
// a WallViewData and keeps its own zoom and pan. Wheel zooms at the cursor, middle drag pans, Home fits.
class WallFaceView : public QGraphicsView {
    Q_OBJECT
public:
    explicit WallFaceView(QWidget* parent = nullptr);

    // Replaces what is drawn. The view fits the face again when the wall changed or it is still in fit mode.
    void setData(const WallViewData& data);
    const WallViewData& data() const { return data_; }
    // Fits the whole face in the view and keeps it fitted while the view is resized.
    void fit();

    // Between face coordinates (u along the wall, v up, metres) and canvas pixels.
    QPointF toView(double u, double v) const;
    QPointF toFace(const QPointF& view) const;
    double pixelsPerMetre() const { return scale_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void enterEvent(QEnterEvent* event) override;

private:
    void applyFit();
    void paintFace(QPainter& painter);
    void paintRuler(QPainter& painter);
    void paintOpenings(QPainter& painter);
    void paintNotes(QPainter& painter);

    WallViewData data_;
    double scale_ = 1.0;   // canvas pixels per metre
    QPointF origin_;       // canvas position of u = 0 (after mirroring), v = 0
    bool fitMode_ = true;
    bool panning_ = false;
    QPoint lastPan_;
};

}
