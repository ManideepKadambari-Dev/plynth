#include "FurnitureTool.h"

#include "canvas/PlanCanvas.h"
#include "furniture/FurniturePicker.h"
#include "theme/Theme.h"
#include "tools/ToolController.h"

#include <plynth/core/Generators.h>
#include <plynth/core/Items.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/catalog/ItemAssets.h>

#include <QGuiApplication>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace plynth::app {

// The status bar hint for this tool.
QString FurnitureTool::hint() const
{
    return QObject::tr("Click to place furniture. Hover an asset to see it.");
}

// Starts with nothing open and listens to the picker.
void FurnitureTool::activate()
{
    click_.reset();
    hoveredAsset_.clear();
    hoveredGenerator_.clear();
    preview_.reset();
    connectPicker();
}

// Closes the picker and forgets the click.
void FurnitureTool::deactivate()
{
    if (host_.pickerOpen())
        host_.picker().dismiss();
    disconnectPicker();
    click_.reset();
    preview_.reset();
    hoveredAsset_.clear();
    hoveredGenerator_.clear();
    host_.canvas().viewport()->update();
}

// A left click marks the place and opens the picker there, with the type of the room under the click.
void FurnitureTool::mousePressed(const ToolMouse& mouse)
{
    if (mouse.button != Qt::LeftButton)
        return;
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (!ppm.has_value())
        return;
    const QPoint inCanvas = host_.canvas().viewport()->mapTo(&host_.canvas(), mouse.view);
    openAt(plynth::Vec2{mouse.scene.x() / *ppm, mouse.scene.y() / *ppm}, inCanvas);
}

// Opens the picker at a place on the plan. The view point is where the picker is anchored in canvas pixels.
void FurnitureTool::openAt(plynth::Vec2 metres, const QPoint& viewPoint)
{
    const plynth::Floor* floor = host_.floor();
    if (floor == nullptr || host_.catalog() == nullptr)
        return;
    click_ = metres;
    preview_.reset();
    hoveredAsset_.clear();
    hoveredGenerator_.clear();
    QString roomType;
    const plynth::Id roomId = plynth::roomAt(*floor, metres);
    if (!roomId.empty())
        for (const plynth::Room& room : floor->rooms)
            if (room.id == roomId && room.type.has_value())
                roomType = QString::fromStdString(*room.type);
    host_.picker().openAt(viewPoint, roomType);
    host_.canvas().viewport()->update();
}

// Hovers an asset in the open picker.
void FurnitureTool::hover(const QString& assetId)
{
    if (host_.pickerOpen())
        host_.picker().hoverAsset(assetId);
}

// Ctrl changes the snapping, so the hovered footprint is worked out again.
void FurnitureTool::modifiersChanged()
{
    updatePreview();
}

// Connects the picker to this tool.
void FurnitureTool::connectPicker()
{
    FurniturePicker& picker = host_.picker();
    chosen_ = QObject::connect(&picker, &FurniturePicker::assetChosen, &host_, [this](const QString& id) { assetChosen(id); });
    hovered_ = QObject::connect(&picker, &FurniturePicker::assetHovered, &host_, [this](const QString& id) { assetHovered(id); });
    generatorChosen_ = QObject::connect(&picker, &FurniturePicker::generatorChosen, &host_, [this](const QString& id) { generatorChosen(id); });
    generatorHovered_ = QObject::connect(&picker, &FurniturePicker::generatorHovered, &host_, [this](const QString& id) { generatorHovered(id); });
    dismissed_ = QObject::connect(&picker, &FurniturePicker::dismissed, &host_, [this]() {
        click_.reset();
        preview_.reset();
        hoveredAsset_.clear();
        hoveredGenerator_.clear();
        host_.canvas().viewport()->update();
    });
}

// Stops listening to the picker.
void FurnitureTool::disconnectPicker()
{
    QObject::disconnect(chosen_);
    QObject::disconnect(hovered_);
    QObject::disconnect(generatorChosen_);
    QObject::disconnect(generatorHovered_);
    QObject::disconnect(dismissed_);
}

// Remembers the hovered asset and works out its footprint at the click point.
void FurnitureTool::assetHovered(const QString& assetId)
{
    hoveredAsset_ = assetId;
    hoveredGenerator_.clear();
    updatePreview();
}

// Remembers the hovered generator and works out its footprint at the click point.
void FurnitureTool::generatorHovered(const QString& generatorId)
{
    hoveredGenerator_ = generatorId;
    hoveredAsset_.clear();
    updatePreview();
}

// Builds the footprint of the hovered asset at the snapped click point, and checks it for problems.
void FurnitureTool::updatePreview()
{
    preview_.reset();
    previewProblem_ = false;
    const plynth::Floor* floor = host_.floor();
    plynth::AssetCatalog* catalog = host_.catalog();
    if (click_.has_value() && floor != nullptr && !hoveredGenerator_.isEmpty()) {
        std::optional<plynth::Item> item = host_.generatorItem(hoveredGenerator_);
        if (item.has_value()) {
            const bool free = QGuiApplication::keyboardModifiers() & Qt::ControlModifier;
            const plynth::Vec2 at = host_.snapItem(*item, *click_, free);
            item->x = at.x;
            item->y = at.y;
            previewProblem_ = !plynth::checkItem(*floor, *item, plynth::AssetLookup{}).empty();
            preview_ = item;
        }
    } else if (click_.has_value() && floor != nullptr && catalog != nullptr && !hoveredAsset_.isEmpty()) {
        const plynth::ItemAsset asset = plynth::itemAssetFor(*catalog, hoveredAsset_.toStdString());
        if (asset.status.ok()) {
            plynth::Item item;
            item.asset = hoveredAsset_.toStdString();
            item.size = asset.size;
            const bool free = QGuiApplication::keyboardModifiers() & Qt::ControlModifier;
            const plynth::Vec2 at = host_.snapItem(item, *click_, free);
            item.x = at.x;
            item.y = at.y;
            previewProblem_ = !plynth::checkItem(*floor, item, plynth::assetLookupFor(*catalog)).empty();
            preview_ = item;
        }
    }
    host_.canvas().viewport()->update();
}

// Places the chosen asset at the click point and selects it.
void FurnitureTool::assetChosen(const QString& assetId)
{
    if (!click_.has_value())
        return;
    const plynth::Vec2 at = *click_;
    const bool free = QGuiApplication::keyboardModifiers() & Qt::ControlModifier;
    click_.reset();
    preview_.reset();
    host_.placeAsset(assetId, at, free);
    host_.canvas().viewport()->update();
}

// Places the chosen generator at the click point and selects it.
void FurnitureTool::generatorChosen(const QString& generatorId)
{
    if (!click_.has_value())
        return;
    const plynth::Vec2 at = *click_;
    const bool free = QGuiApplication::keyboardModifiers() & Qt::ControlModifier;
    click_.reset();
    preview_.reset();
    host_.placeGenerator(generatorId, at, free);
    host_.canvas().viewport()->update();
}

// Draws the hovered footprint with its front tick, and the amber cross at the click while the picker is open.
void FurnitureTool::paint(QPainter& painter)
{
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (!click_.has_value() || !ppm.has_value())
        return;
    const Theme& t = Theme::instance();
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (preview_.has_value()) {
        const plynth::ItemFootprint footprint = plynth::itemFootprint(*preview_);
        QPolygonF shape;
        for (const plynth::Vec2& corner : footprint.corners)
            shape << host_.canvas().sceneToView(QPointF(corner.x * *ppm, corner.y * *ppm));
        QColor fill = t.raised();
        fill.setAlpha(t.itemFillAlpha());
        QPen outline(previewProblem_ ? t.signalAmber() : t.text(), 1.5);
        if (previewProblem_)
            outline.setDashPattern({4.0, 3.0});
        painter.setBrush(fill);
        painter.setPen(outline);
        painter.drawPolygon(shape);
        const QPointF frontMid = (shape[2] + shape[3]) / 2.0;
        const QPointF centre = (shape[0] + shape[2]) / 2.0;
        const QPointF toFront = frontMid - centre;
        const double reach = std::hypot(toFront.x(), toFront.y());
        if (reach > 1.0) {
            painter.setPen(QPen(outline.color(), 2.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(frontMid, frontMid - toFront / reach * std::min(reach / 3.0, double(t.unit() * 3)));
        }
    }
    const QPointF at = host_.canvas().sceneToView(QPointF(click_->x * *ppm, click_->y * *ppm));
    const double arm = t.unit() * 2.0;
    painter.setPen(QPen(t.signalAmber(), 2.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(at - QPointF(arm, 0), at + QPointF(arm, 0));
    painter.drawLine(at - QPointF(0, arm), at + QPointF(0, arm));
}

}
