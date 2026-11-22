#pragma once

#include "tools/Tool.h"

#include <plynth/core/Geometry.h>
#include <plynth/core/Plan.h>

#include <QMetaObject>
#include <QPoint>
#include <QString>

#include <optional>

namespace plynth::app {

// Places furniture: a click shows a marker and opens the picker there, filtered by the room under the click.
// Hovering an asset in the picker draws its footprint at the click point, and choosing it places the item.
class FurnitureTool : public Tool {
public:
    explicit FurnitureTool(ToolController& host) : Tool(host) {}

    ToolId id() const override { return ToolId::Furniture; }
    QString hint() const override;

    void activate() override;
    void deactivate() override;
    void mousePressed(const ToolMouse& mouse) override;
    void modifiersChanged() override;
    void paint(QPainter& painter) override;

    // Opens the picker at a place on the plan in metres, as a click would. viewPoint is where the picker is
    // anchored, in canvas pixels. Used by the click, and by the checks.
    void openAt(plynth::Vec2 metres, const QPoint& viewPoint);
    // Hovers an asset in the open picker, as the mouse would.
    void hover(const QString& assetId);

private:
    void connectPicker();
    void disconnectPicker();
    void assetHovered(const QString& assetId);
    void assetChosen(const QString& assetId);
    void generatorHovered(const QString& generatorId);
    void generatorChosen(const QString& generatorId);
    void updatePreview();

    QMetaObject::Connection chosen_;
    QMetaObject::Connection hovered_;
    QMetaObject::Connection generatorChosen_;
    QMetaObject::Connection generatorHovered_;
    QMetaObject::Connection dismissed_;
    std::optional<plynth::Vec2> click_;
    QString hoveredAsset_;
    QString hoveredGenerator_;
    std::optional<plynth::Item> preview_;
    bool previewProblem_ = false;
};

}
