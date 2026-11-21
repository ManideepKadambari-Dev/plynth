#include "PlanCanvas.h"

#include "theme/Theme.h"

#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace plynth::app {

namespace {

constexpr double kMinZoom = 0.02;
constexpr double kMaxZoom = 32.0;
constexpr double kStep = 1.25;
constexpr double kNotch = 120.0;
constexpr double kSmoothBelow = 4.0;

}

// Sets up the scene, hides the scroll bars and lets the view take keyboard focus.
PlanCanvas::PlanCanvas(QWidget* parent)
    : QGraphicsView(parent), scene_(new QGraphicsScene(this))
{
    setScene(scene_);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setDragMode(QGraphicsView::NoDrag);
    setToolTip(tr("Plan sheet. Wheel zooms at the cursor. Middle drag or Space and drag pans."));
    setProperty("tourId", QStringLiteral("canvas-plan"));
    emptyText_ = defaultEmptyText();
    updateCursor();
}

// Shows a new image on the sheet and fits it to the window.
void PlanCanvas::setImage(const QImage& image)
{
    if (pixmapItem_ != nullptr) {
        scene_->removeItem(pixmapItem_);
        delete pixmapItem_;
        pixmapItem_ = nullptr;
    }
    if (image.isNull()) {
        updateSceneRect();
        viewport()->update();
        return;
    }
    pixmapItem_ = scene_->addPixmap(QPixmap::fromImage(image));
    pixmapItem_->setPos(0, 0);
    pixmapItem_->setZValue(1);
    fitToWindow();
}

// The hint shown on an empty sheet when nothing else needs saying.
QString PlanCanvas::defaultEmptyText()
{
    return tr("Set a plan image with Ctrl+Shift+O or drop one here");
}

// Sets the line shown in the middle of the empty sheet. Empty text brings back the hint.
void PlanCanvas::setEmptyText(const QString& text)
{
    emptyText_ = text.isEmpty() ? defaultEmptyText() : text;
    viewport()->update();
}

// Removes the image and shows the empty state.
void PlanCanvas::clearImage()
{
    setImage(QImage());
    emit zoomChanged(zoom_);
}

// Zooms in one step around the centre of the view.
void PlanCanvas::zoomIn()
{
    if (!hasImage())
        return;
    fitMode_ = false;
    setZoomAt(zoom_ * kStep, viewport()->rect().center());
}

// Zooms out one step around the centre of the view.
void PlanCanvas::zoomOut()
{
    if (!hasImage())
        return;
    fitMode_ = false;
    setZoomAt(zoom_ / kStep, viewport()->rect().center());
}

// Fits the whole sheet in the window and keeps it fitted while the window is resized.
void PlanCanvas::fitToWindow()
{
    if (!hasImage())
        return;
    fitMode_ = true;
    applyFit();
}

// Shows the image at 100 percent around the centre of the view.
void PlanCanvas::actualSize()
{
    if (!hasImage())
        return;
    fitMode_ = false;
    setZoomAt(1.0, viewport()->rect().center());
}

// Works out the zoom that shows the image and its padding, and centres it.
void PlanCanvas::applyFit()
{
    const int pad = Theme::instance().sheetPadding();
    const QRectF img = imageRect();
    const double availW = std::max(1, viewport()->width() - 2 * pad);
    const double availH = std::max(1, viewport()->height() - 2 * pad);
    const double z = std::clamp(std::min(availW / img.width(), availH / img.height()), kMinZoom, kMaxZoom);
    zoom_ = z;
    resetTransform();
    scale(zoom_, zoom_);
    updateSceneRect();
    centerOn(img.center());
    pixmapItem_->setTransformationMode(zoom_ >= kSmoothBelow ? Qt::FastTransformation
                                                             : Qt::SmoothTransformation);
    viewport()->update();
    emit zoomChanged(zoom_);
}

// Changes the zoom so the scene point under the anchor stays under the anchor.
void PlanCanvas::setZoomAt(double zoom, const QPoint& viewportAnchor)
{
    const double z = std::clamp(zoom, kMinZoom, kMaxZoom);
    const QPointF scenePoint = mapToScene(viewportAnchor);
    zoom_ = z;
    resetTransform();
    scale(zoom_, zoom_);
    updateSceneRect();
    const QPoint now = mapFromScene(scenePoint);
    const QPoint delta = now - viewportAnchor;
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + delta.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() + delta.y());
    pixmapItem_->setTransformationMode(zoom_ >= kSmoothBelow ? Qt::FastTransformation
                                                             : Qt::SmoothTransformation);
    viewport()->update();
    emit zoomChanged(zoom_);
}

// The image rectangle in scene units, or an empty rectangle with no image.
QRectF PlanCanvas::imageRect() const
{
    if (pixmapItem_ == nullptr)
        return QRectF();
    return QRectF(QPointF(0, 0), QSizeF(pixmapItem_->pixmap().size()));
}

// Grows the scene around the image so it can be pushed partly off screen but never lost.
void PlanCanvas::updateSceneRect()
{
    if (pixmapItem_ == nullptr) {
        setSceneRect(QRectF());
        return;
    }
    const double margin = std::max(viewport()->width(), viewport()->height()) / zoom_;
    setSceneRect(imageRect().adjusted(-margin, -margin, margin, margin));
}

// Paints the Ground canvas and the Raised sheet with its border.
void PlanCanvas::drawBackground(QPainter* painter, const QRectF& rect)
{
    const Theme& t = Theme::instance();
    painter->fillRect(rect, t.ground());
    if (pixmapItem_ == nullptr)
        return;

    const QRect img = mapFromScene(imageRect()).boundingRect();
    const int pad = t.sheetPadding();
    const QRect sheet = img.adjusted(-pad, -pad, pad, pad);
    painter->save();
    painter->resetTransform();
    painter->fillRect(sheet, t.raised());
    painter->setPen(QPen(t.line(), 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(sheet.adjusted(0, 0, -1, -1));
    painter->restore();
}

// Paints the hint line in the middle of the view when no image is loaded.
void PlanCanvas::drawForeground(QPainter* painter, const QRectF& rect)
{
    Q_UNUSED(rect);
    if (pixmapItem_ != nullptr)
        return;
    const Theme& t = Theme::instance();
    painter->save();
    painter->resetTransform();
    painter->setPen(t.muted());
    painter->setFont(t.uiFont(t.sizeNormal()));
    painter->drawText(viewport()->rect(), Qt::AlignCenter,
                      emptyText_);
    painter->restore();
}

// Zooms at the cursor. One notch is 1.25 times, touchpad deltas count in proportion.
void PlanCanvas::wheelEvent(QWheelEvent* event)
{
    if (!hasImage()) {
        event->ignore();
        return;
    }
    const double delta = event->angleDelta().y();
    if (delta == 0.0) {
        event->ignore();
        return;
    }
    fitMode_ = false;
    setZoomAt(zoom_ * std::pow(kStep, delta / kNotch), event->position().toPoint());
    event->accept();
}

// Starts a pan on middle button, or on left button while Space is held.
void PlanCanvas::mousePressEvent(QMouseEvent* event)
{
    const bool middle = event->button() == Qt::MiddleButton;
    const bool spaceLeft = event->button() == Qt::LeftButton && spaceHeld_;
    if (hasImage() && (middle || spaceLeft)) {
        panning_ = true;
        panButton_ = event->button();
        lastPanPos_ = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

// Moves the view while a pan is in progress.
void PlanCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (panning_) {
        const QPoint d = event->pos() - lastPanPos_;
        lastPanPos_ = event->pos();
        if (!d.isNull())
            fitMode_ = false;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

// Ends a pan.
void PlanCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (panning_ && event->button() == panButton_) {
        panning_ = false;
        panButton_ = Qt::NoButton;
        updateCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

// Holding Space turns the left button into a pan button.
void PlanCanvas::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space) {
        if (!event->isAutoRepeat()) {
            spaceHeld_ = true;
            updateCursor();
        }
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

// Releasing Space returns the left button to normal.
void PlanCanvas::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space) {
        if (!event->isAutoRepeat()) {
            spaceHeld_ = false;
            updateCursor();
        }
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

// Drops Space and pan state when focus leaves, since the release would go elsewhere.
void PlanCanvas::focusOutEvent(QFocusEvent* event)
{
    QGraphicsView::focusOutEvent(event);
    spaceHeld_ = false;
    panning_ = false;
    panButton_ = Qt::NoButton;
    updateCursor();
}

// Takes keyboard focus when the mouse comes in, so Space works without a click.
void PlanCanvas::enterEvent(QEnterEvent* event)
{
    QGraphicsView::enterEvent(event);
    setFocus(Qt::MouseFocusReason);
}

// Keeps the sheet fitted after a resize while the user has not zoomed or panned.
void PlanCanvas::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if (!hasImage())
        return;
    if (fitMode_)
        applyFit();
    else
        updateSceneRect();
}

// Fits once the real size is known, in case the image was loaded before the first show.
void PlanCanvas::showEvent(QShowEvent* event)
{
    QGraphicsView::showEvent(event);
    if (hasImage() && fitMode_)
        applyFit();
}

// Shows an open hand while Space is held, otherwise the normal arrow.
void PlanCanvas::updateCursor()
{
    if (panning_)
        viewport()->setCursor(Qt::ClosedHandCursor);
    else if (spaceHeld_)
        viewport()->setCursor(Qt::OpenHandCursor);
    else
        viewport()->setCursor(Qt::ArrowCursor);
}

}
