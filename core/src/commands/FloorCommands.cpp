#include "plynth/core/commands/FloorCommands.h"

#include "CommandSupport.h"

#include <utility>

namespace plynth {

SetFloorImageCommand::SetFloorImageCommand(Id floorId, std::optional<std::string> image)
    : floorId_(std::move(floorId)), image_(std::move(image))
{
}

std::string SetFloorImageCommand::name() const
{
    return image_ ? "Set plan image" : "Clear plan image";
}

// Sets or clears the image of the floor, remembering the old one.
Status SetFloorImageCommand::apply(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");
    if (image_ && image_->empty()) return fail(ErrorCode::InvalidValue, "The plan image path is empty.");
    previous_ = floor->image;
    floor->image = image_;
    return {};
}

// Puts the old image back.
void SetFloorImageCommand::undo(Project& project)
{
    if (Floor* floor = findFloor(project.plan, floorId_)) floor->image = previous_;
}

unsigned SetFloorImageCommand::changes() const
{
    return Change::FloorImage;
}

Id SetFloorImageCommand::floorId() const
{
    return floorId_;
}

SetScaleCommand::SetScaleCommand(std::optional<double> pixelsPerMetre) : pixelsPerMetre_(pixelsPerMetre) {}

std::string SetScaleCommand::name() const
{
    return pixelsPerMetre_ ? "Set scale" : "Clear scale";
}

// Sets or clears the scale, remembering the old one.
Status SetScaleCommand::apply(Project& project)
{
    if (pixelsPerMetre_ && !detail::isPositive(*pixelsPerMetre_)) {
        return fail(ErrorCode::InvalidValue, "The scale must be a number above zero.");
    }
    previous_ = project.plan.scale.pixelsPerMetre;
    project.plan.scale.pixelsPerMetre = pixelsPerMetre_;
    return {};
}

// Puts the old scale back.
void SetScaleCommand::undo(Project& project)
{
    project.plan.scale.pixelsPerMetre = previous_;
}

unsigned SetScaleCommand::changes() const
{
    return Change::Scale;
}

}
