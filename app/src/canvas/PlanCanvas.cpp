#include "PlanCanvas.h"

#include "canvas/OpeningPaint.h"
#include "theme/Theme.h"
#include "tools/Tool.h"

#include <QApplication>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QFontMetrics>
#include <QPolygonF>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numbers>

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
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
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

// Greys the sheet while the project is read only. The overlay stays on top at full strength.
void PlanCanvas::setDimmed(bool dimmed)
{
    dimmed_ = dimmed;
    viewport()->update();
}

// Replaces what is drawn over the plan image. Positions are in scene units.
void PlanCanvas::setOverlay(const OverlayData& overlay)
{
    overlay_ = overlay;
    viewport()->update();
}

// Replaces the detection preview. Positions are in scene units.
void PlanCanvas::setProposals(const ProposalOverlay& proposals)
{
    proposals_ = proposals;
    viewport()->update();
}

// Says which item is drawn in the selection color.
void PlanCanvas::setSelection(SelectionKind kind, const QString& id)
{
    selectionKind_ = kind;
    selectionId_ = id;
    viewport()->update();
}

// Sets the grid step in scene units. 0 hides the grid.
void PlanCanvas::setGridStep(double scenePixels)
{
    gridStep_ = scenePixels;
    viewport()->update();
}

// Gives the mouse, keys and previews to a tool.
void PlanCanvas::setTool(Tool* tool)
{
    tool_ = tool;
    updateCursor();
    viewport()->update();
}

// Where a scene position is in the pixels of the canvas.
QPointF PlanCanvas::sceneToView(const QPointF& scene) const
{
    return mapFromScene(scene);
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
    const Theme& t = Theme::instance();
    if (pixmapItem_ != nullptr) {
        if (dimmed_) {
            QColor dim = t.ground();
            dim.setAlpha(t.readOnlyDim());
            const int pad = t.sheetPadding();
            painter->save();
            painter->resetTransform();
            painter->fillRect(mapFromScene(imageRect()).boundingRect().adjusted(-pad, -pad, pad, pad), dim);
            painter->restore();
        }
        paintOverlay(painter);
        return;
    }
    painter->save();
    painter->resetTransform();
    painter->setPen(t.muted());
    painter->setFont(t.uiFont(t.sizeNormal()));
    painter->drawText(viewport()->rect(), Qt::AlignCenter,
                      emptyText_);
    painter->restore();
}

// Paints grid, rooms, walls, points, labels, the selection and the tool previews, in that order.
void PlanCanvas::paintOverlay(QPainter* painter)
{
    const Theme& t = Theme::instance();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    if (gridStep_ > 0.0 && gridStep_ * zoom_ >= 8.0) {
        const QRectF img = imageRect();
        painter->setRenderHint(QPainter::Antialiasing, false);
        QColor gridColor = t.line();
        gridColor.setAlpha(t.overlayAlpha());
        painter->setPen(QPen(gridColor, 0));
        for (double x = 0.0; x <= img.width(); x += gridStep_)
            painter->drawLine(QPointF(x, 0.0), QPointF(x, img.height()));
        for (double y = 0.0; y <= img.height(); y += gridStep_)
            painter->drawLine(QPointF(0.0, y), QPointF(img.width(), y));
        painter->setRenderHint(QPainter::Antialiasing, true);
    }

    QColor roomBlue = t.signalBlue();
    roomBlue.setAlpha(t.roomFillAlpha());
    painter->setPen(Qt::NoPen);
    painter->setBrush(roomBlue);
    for (const OverlayRoom& room : overlay_.rooms)
        if (!(selectionKind_ == SelectionKind::Room && room.id == selectionId_))
            painter->drawPath(room.floor);

    QColor blue = t.signalBlue();
    blue.setAlpha(t.overlayAlpha());
    painter->setBrush(blue);
    for (const OverlayWall& wall : overlay_.walls)
        painter->drawPath(wall.band);

    if (selectionKind_ == SelectionKind::Room) {
        QColor amberFill = t.signalAmber();
        amberFill.setAlpha(t.roomFillAlpha());
        QPen outline(t.signalAmber());
        outline.setCosmetic(true);
        outline.setWidth(2);
        for (const OverlayRoom& room : overlay_.rooms) {
            if (room.id != selectionId_)
                continue;
            painter->setPen(Qt::NoPen);
            painter->setBrush(amberFill);
            painter->drawPath(room.floor);
            painter->setPen(outline);
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(room.floor);
        }
    }
    if (selectionKind_ == SelectionKind::Wall) {
        QColor amber = t.signalAmber();
        amber.setAlpha(230);
        painter->setPen(Qt::NoPen);
        painter->setBrush(amber);
        for (const OverlayWall& wall : overlay_.walls)
            if (wall.id == selectionId_)
                painter->drawPath(wall.band);
    }

    painter->resetTransform();
    {
        const QTransform view = viewportTransform();
        OpeningPaintStyle style;
        style.gapColor = t.raised();
        style.outlineColor = t.signalAmber();
        for (const OverlayOpening& opening : overlay_.openings) {
            const bool selected = selectionKind_ == SelectionKind::Opening && opening.id == selectionId_;
            style.line = selected ? t.signalAmber() : t.signalBlue();
            style.dashedFootprint = opening.problem;
            paintOpening(*painter, opening, view, style);
        }
    }
    painter->setPen(Qt::NoPen);
    painter->setBrush(t.signalBlue());
    const double radius = t.pointDot() / 2.0;
    for (const OverlayPoint& point : overlay_.points)
        painter->drawEllipse(QPointF(mapFromScene(point.position)), radius, radius);

    paintLabels(painter);

    if (selectionKind_ == SelectionKind::Point) {
        for (const OverlayPoint& point : overlay_.points) {
            if (point.id != selectionId_)
                continue;
            const QPointF at = mapFromScene(point.position);
            painter->setPen(Qt::NoPen);
            painter->setBrush(t.signalAmber());
            painter->drawEllipse(at, radius, radius);
            painter->setPen(QPen(t.signalAmber(), 2));
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(at, radius + 4, radius + 4);
        }
    }

    paintProposals(painter);

    if (tool_ != nullptr)
        tool_->paint(*painter);
    painter->restore();
}

// Paints the detection preview in canvas pixels: dashed blue wall bands, room outlines, point rings and
// opening marks. It is never amber, because amber is for what the user picked.
void PlanCanvas::paintProposals(QPainter* painter)
{
    if (proposals_.isEmpty())
        return;
    const Theme& t = Theme::instance();
    const QTransform view = viewportTransform();
    QColor line = t.signalBlue();
    line.setAlpha(t.proposalLineAlpha());
    QColor fill = t.signalBlue();
    fill.setAlpha(t.proposalFillAlpha());
    QPen dashed(line, 1.5);
    dashed.setDashPattern({double(t.proposalDash()), double(t.proposalDash())});
    dashed.setCapStyle(Qt::FlatCap);
    const QPen solid(line, 1.5);

    painter->setBrush(fill);
    painter->setPen(dashed);
    for (const ProposalWall& wall : proposals_.walls) {
        const QPointF delta = wall.to - wall.from;
        const double length = std::hypot(delta.x(), delta.y());
        if (length <= 0.0)
            continue;
        const QPointF dir = delta / length;
        const QPointF normal(-dir.y(), dir.x());
        const double half = wall.thickness / 2.0;
        const QPointF a = wall.from - dir * half;
        const QPointF b = wall.to + dir * half;
        painter->drawPolygon(view.map(QPolygonF({a + normal * half, b + normal * half, b - normal * half,
                                                     a - normal * half})));
    }

    painter->setBrush(Qt::NoBrush);
    painter->setPen(dashed);
    for (const QVector<QPolygonF>& rings : proposals_.rooms)
        for (const QPolygonF& ring : rings)
            painter->drawPolygon(view.map(ring));

    painter->setPen(solid);
    for (const ProposalOpening& opening : proposals_.openings) {
        const QPointF dir = opening.direction;
        const QPointF normal(-dir.y(), dir.x());
        const double half = opening.thickness / 2.0;
        const QPointF a = opening.centre - dir * (opening.width / 2.0);
        const QPointF b = opening.centre + dir * (opening.width / 2.0);
        const auto tick = [&](const QPointF& at) {
            painter->drawLine(view.map(at + normal * half), view.map(at - normal * half));
        };
        tick(a);
        tick(b);
        if (opening.kind == 1) {
            QPolygonF arc;
            constexpr int kSteps = 16;
            for (int i = 0; i <= kSteps; ++i) {
                const double angle = (std::numbers::pi / 2.0) * i / kSteps;
                arc << a + (b - a) * std::cos(angle) + normal * (opening.width * std::sin(angle));
            }
            painter->drawPolyline(view.map(arc));
            painter->drawLine(view.map(a), view.map(a + normal * opening.width));
        } else if (opening.kind == 2) {
            const QPointF shift = normal * (opening.thickness / 6.0);
            painter->drawLine(view.map(a + shift), view.map(b + shift));
            painter->drawLine(view.map(a - shift), view.map(b - shift));
        }
    }

    painter->setPen(solid);
    const double radius = t.pointDot() / 2.0 + 1.0;
    for (const QPointF& point : proposals_.points)
        painter->drawEllipse(view.map(point), radius, radius);
}

// Paints each room's name and area at constant screen size, unless the room is too small for them.
void PlanCanvas::paintLabels(QPainter* painter)
{
    const Theme& t = Theme::instance();
    const QFont nameFont = t.uiFont(t.sizeNormal());
    const QFont areaFont = t.monoFont(t.sizeSmall());
    const QFontMetrics nameMetrics(nameFont);
    const QFontMetrics areaMetrics(areaFont);
    for (const OverlayRoom& room : overlay_.rooms) {
        const int nameWidth = nameMetrics.horizontalAdvance(room.name);
        const int areaWidth = areaMetrics.horizontalAdvance(room.area);
        const double width = std::max(nameWidth, areaWidth);
        const QRectF roomBox = mapFromScene(room.floor.boundingRect()).boundingRect();
        if (width + t.unit() * 2 > roomBox.width())
            continue;
        const int height = nameMetrics.height() + areaMetrics.height();
        if (height > roomBox.height())
            continue;
        const QPointF at = mapFromScene(room.label);
        const QRectF nameBox(at.x() - width / 2.0, at.y() - height / 2.0, width, nameMetrics.height());
        const QRectF areaBox(at.x() - width / 2.0, nameBox.bottom(), width, areaMetrics.height());
        const QRectF pill = QRectF(at.x() - width / 2.0, at.y() - height / 2.0, width, height)
                                .adjusted(-t.unit(), -t.unit() / 2.0, t.unit(), t.unit() / 2.0);
        QColor backing = t.panel();
        backing.setAlpha(230);
        painter->setPen(Qt::NoPen);
        painter->setBrush(backing);
        painter->drawRoundedRect(pill, t.radius(), t.radius());
        painter->setFont(nameFont);
        painter->setPen(t.text());
        painter->drawText(nameBox, Qt::AlignCenter, room.name);
        painter->setFont(areaFont);
        painter->setPen(t.muted());
        painter->drawText(areaBox, Qt::AlignCenter, room.area);
    }
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

namespace {

// Builds the tool event for a mouse event, in scene and canvas coordinates.
ToolMouse toolMouse(const PlanCanvas& canvas, const QMouseEvent* event)
{
    ToolMouse mouse;
    mouse.view = event->position().toPoint();
    mouse.scene = canvas.mapToScene(mouse.view);
    mouse.global = event->globalPosition().toPoint();
    mouse.button = event->button();
    mouse.buttons = event->buttons();
    mouse.modifiers = event->modifiers();
    return mouse;
}

}

// Starts a pan on middle button, or on left button while Space is held. Other clicks go to the tool.
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
    if (hasImage() && tool_ != nullptr) {
        tool_->mousePressed(toolMouse(*this, event));
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

// Moves the view while a pan is in progress, otherwise tells the tool and the status bar.
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
    if (hasImage()) {
        const ToolMouse mouse = toolMouse(*this, event);
        emit cursorMoved(mouse.scene);
        if (tool_ != nullptr)
            tool_->mouseMoved(mouse);
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

// Ends a pan, or tells the tool the button was released.
void PlanCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (panning_ && event->button() == panButton_) {
        panning_ = false;
        panButton_ = Qt::NoButton;
        updateCursor();
        event->accept();
        return;
    }
    if (hasImage() && tool_ != nullptr) {
        tool_->mouseReleased(toolMouse(*this, event));
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

// Tells the tool and the status bar that the cursor left the canvas.
void PlanCanvas::leaveEvent(QEvent* event)
{
    QGraphicsView::leaveEvent(event);
    emit cursorLeft();
    if (tool_ != nullptr)
        tool_->mouseLeft();
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
    if (event->key() == Qt::Key_Shift || event->key() == Qt::Key_Control) {
        if (tool_ != nullptr)
            tool_->modifiersChanged();
    } else if (tool_ != nullptr && tool_->keyPressed(event)) {
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
    if ((event->key() == Qt::Key_Shift || event->key() == Qt::Key_Control) && tool_ != nullptr)
        tool_->modifiersChanged();
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
    if (qobject_cast<QLineEdit*>(QApplication::focusWidget()) == nullptr)
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
        viewport()->setCursor(tool_ != nullptr ? tool_->cursor() : Qt::ArrowCursor);
}

}
