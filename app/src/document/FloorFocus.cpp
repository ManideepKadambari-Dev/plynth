#include "FloorFocus.h"

#include <plynth/core/Plan.h>

namespace plynth::app {

FloorFocus::FloorFocus(const plynth::Document& document, QObject* parent) : QObject(parent), document_(document)
{
    revalidate();
}

// The floor that is shown, or nothing when the plan has no floors.
const plynth::Floor* FloorFocus::floor() const
{
    const plynth::Plan& plan = document_.plan();
    const std::string id = id_.toStdString();
    for (const plynth::Floor& candidate : plan.floors)
        if (candidate.id == id)
            return &candidate;
    return plan.floors.empty() ? nullptr : &plan.floors.front();
}

// The position of the shown floor in the plan.
int FloorFocus::index() const
{
    const plynth::Floor* shown = floor();
    if (shown == nullptr)
        return 0;
    const std::optional<std::size_t> at = plynth::floorIndex(document_.plan(), shown->id);
    return at.has_value() ? static_cast<int>(*at) : 0;
}

// Switches to a floor of the plan and tells the views.
bool FloorFocus::setFloorId(const QString& id)
{
    if (!plynth::floorIndex(document_.plan(), id.toStdString()).has_value())
        return false;
    if (id == id_)
        return true;
    id_ = id;
    emit changed(id_);
    return true;
}

// Keeps the shown floor valid after the plan changed, for example after undo of an added floor.
void FloorFocus::revalidate()
{
    const plynth::Plan& plan = document_.plan();
    if (plan.floors.empty()) {
        if (!id_.isEmpty()) {
            id_.clear();
            emit changed(id_);
        }
        return;
    }
    if (!id_.isEmpty() && plynth::floorIndex(plan, id_.toStdString()).has_value())
        return;
    id_ = QString::fromStdString(plan.floors.front().id);
    emit changed(id_);
}

}
