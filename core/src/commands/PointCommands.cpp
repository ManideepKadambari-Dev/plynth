#include "plynth/core/commands/PointCommands.h"

#include "CommandSupport.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace plynth {

AddPointCommand::AddPointCommand(Id floorId, double x, double y, double cornerRadius) : floorId_(std::move(floorId))
{
    point_.x = x;
    point_.y = y;
    point_.cornerRadius = cornerRadius;
}

// The id of the new point, empty until the first apply.
const Id& AddPointCommand::pointId() const
{
    return point_.id;
}

std::string AddPointCommand::name() const
{
    return "Add point";
}

// Adds the point at the end of the floor's list, with a new id on the first call and the same id on redo.
Status AddPointCommand::apply(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");
    if (!std::isfinite(point_.x) || !std::isfinite(point_.y)) {
        return fail(ErrorCode::InvalidValue, "The point's x and y must be finite numbers.");
    }
    if (!std::isfinite(point_.cornerRadius) || point_.cornerRadius < 0.0) {
        return fail(ErrorCode::InvalidValue, "The corner radius must be zero or more.");
    }
    Point p = point_;
    if (p.id.empty()) {
        p.id = newId(project.plan, kPointIdPrefix);
    } else if (idInUse(project.plan, p.id)) {
        return fail(ErrorCode::AlreadyExists, "The id " + p.id + " is already used.");
    }
    point_.id = p.id;
    floor->points.push_back(std::move(p));
    return {};
}

// Removes the point that apply added.
void AddPointCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    std::erase_if(floor->points, [&](const Point& p) { return p.id == point_.id; });
}

unsigned AddPointCommand::changes() const
{
    return Change::Points;
}

Id AddPointCommand::floorId() const
{
    return floorId_;
}

MovePointCommand::MovePointCommand(Id pointId, double x, double y, int mergeKey)
    : pointId_(std::move(pointId)), x_(x), y_(y), mergeKey_(mergeKey)
{
}

std::string MovePointCommand::name() const
{
    return "Move point";
}

// Moves the point, remembering where it was.
Status MovePointCommand::apply(Project& project)
{
    Floor* floor = detail::floorOfPoint(project.plan, pointId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no point " + pointId_ + ".");
    if (!std::isfinite(x_) || !std::isfinite(y_)) {
        return fail(ErrorCode::InvalidValue, "The point's x and y must be finite numbers.");
    }
    Point* p = findPoint(*floor, pointId_);
    for (const Wall& w : floor->walls) {
        if (w.from != pointId_ && w.to != pointId_) continue;
        const Point* a = findPoint(*floor, w.from);
        const Point* b = findPoint(*floor, w.to);
        if (!a || !b) continue;
        const Vec2 from = w.from == pointId_ ? Vec2{x_, y_} : positionOf(*a);
        const Vec2 to = w.to == pointId_ ? Vec2{x_, y_} : positionOf(*b);
        if (Status s = detail::checkCurveLimits(from, to, w.bulge, w.thickness, w.id); !s.ok()) return s;
    }
    floorId_ = floor->id;
    oldX_ = p->x;
    oldY_ = p->y;
    p->x = x_;
    p->y = y_;
    return {};
}

// Puts the point back where it was.
void MovePointCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    if (Point* p = findPoint(*floor, pointId_)) {
        p->x = oldX_;
        p->y = oldY_;
    }
}

unsigned MovePointCommand::changes() const
{
    return Change::Points;
}

Id MovePointCommand::floorId() const
{
    return floorId_;
}

// Takes over the later target of a move of the same point in the same drag.
bool MovePointCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const MovePointCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->pointId_ != pointId_) return false;
    x_ = other->x_;
    y_ = other->y_;
    return true;
}

RemovePointCommand::RemovePointCommand(Id pointId) : pointId_(std::move(pointId)) {}

std::string RemovePointCommand::name() const
{
    return "Remove point";
}

// Removes the point unless a wall uses it, remembering its place in the list.
Status RemovePointCommand::apply(Project& project)
{
    Floor* floor = detail::floorOfPoint(project.plan, pointId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no point " + pointId_ + ".");
    for (const Wall& w : floor->walls) {
        if (w.from == pointId_ || w.to == pointId_) {
            return fail(ErrorCode::InUse, "The point " + pointId_ + " is used by the wall " + w.id + ".");
        }
    }
    const auto it = std::find_if(floor->points.begin(), floor->points.end(), [&](const Point& p) { return p.id == pointId_; });
    floorId_ = floor->id;
    index_ = static_cast<std::size_t>(it - floor->points.begin());
    removed_ = *it;
    floor->points.erase(it);
    return {};
}

// Puts the point back at its old place.
void RemovePointCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    const std::size_t at = std::min(index_, floor->points.size());
    floor->points.insert(floor->points.begin() + static_cast<std::ptrdiff_t>(at), removed_);
}

unsigned RemovePointCommand::changes() const
{
    return Change::Points;
}

Id RemovePointCommand::floorId() const
{
    return floorId_;
}

}

namespace plynth {

SetCornerRadiusCommand::SetCornerRadiusCommand(Id pointId, double radius, int mergeKey)
    : pointId_(std::move(pointId)), radius_(radius), mergeKey_(mergeKey)
{
}

std::string SetCornerRadiusCommand::name() const
{
    return "Set corner radius";
}

// Sets the corner radius, remembering the old one.
Status SetCornerRadiusCommand::apply(Project& project)
{
    Floor* floor = detail::floorOfPoint(project.plan, pointId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no point " + pointId_ + ".");
    if (!std::isfinite(radius_) || radius_ < 0.0) {
        return fail(ErrorCode::InvalidValue, "The corner radius must be zero or more.");
    }
    Point* p = findPoint(*floor, pointId_);
    floorId_ = floor->id;
    previous_ = p->cornerRadius;
    p->cornerRadius = radius_;
    return {};
}

// Puts the old radius back.
void SetCornerRadiusCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    if (Point* p = findPoint(*floor, pointId_)) p->cornerRadius = previous_;
}

unsigned SetCornerRadiusCommand::changes() const
{
    return Change::Points;
}

Id SetCornerRadiusCommand::floorId() const
{
    return floorId_;
}

// Takes over the later radius of the same point in the same series of changes.
bool SetCornerRadiusCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetCornerRadiusCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->pointId_ != pointId_) return false;
    radius_ = other->radius_;
    return true;
}

}
