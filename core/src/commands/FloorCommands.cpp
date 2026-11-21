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

SetScaleCommand::SetScaleCommand(std::optional<double> pixelsPerMetre, ScaleKeep keep)
    : pixelsPerMetre_(pixelsPerMetre), keep_(keep)
{
}

// True when the last apply moved marks.
bool SetScaleCommand::movedMarks() const
{
    return moved_;
}

std::string SetScaleCommand::name() const
{
    return pixelsPerMetre_ ? "Set scale" : "Clear scale";
}

// Sets or clears the scale. When an old scale changes and marks keep their image place, their metre values are
// rescaled and the floors are saved whole so undo is exact.
Status SetScaleCommand::apply(Project& project)
{
    if (pixelsPerMetre_ && !detail::isPositive(*pixelsPerMetre_)) {
        return fail(ErrorCode::InvalidValue, "The scale must be a number above zero.");
    }
    previous_ = project.plan.scale.pixelsPerMetre;
    moved_ = false;
    previousFloors_.clear();
    if (keep_ == ScaleKeep::ImagePositions && previous_ && pixelsPerMetre_ && *previous_ != *pixelsPerMetre_) {
        bool hasMarks = false;
        for (const Floor& f : project.plan.floors) {
            if (!f.points.empty() || !f.openings.empty()) hasMarks = true;
        }
        if (hasMarks) {
            const double factor = *previous_ / *pixelsPerMetre_;
            previousFloors_ = project.plan.floors;
            for (Floor& f : project.plan.floors) {
                for (Point& p : f.points) {
                    p.x *= factor;
                    p.y *= factor;
                }
                for (Wall& w : f.walls) w.bulge *= factor;
                for (Opening& o : f.openings) o.offset *= factor;
            }
            moved_ = true;
        }
    }
    project.plan.scale.pixelsPerMetre = pixelsPerMetre_;
    return {};
}

// Puts the old scale back, and the old floors when marks were moved.
void SetScaleCommand::undo(Project& project)
{
    project.plan.scale.pixelsPerMetre = previous_;
    if (moved_) project.plan.floors = previousFloors_;
}

unsigned SetScaleCommand::changes() const
{
    return moved_ ? (Change::Scale | Change::Points | Change::Walls | Change::Openings) : Change::Scale;
}

}
