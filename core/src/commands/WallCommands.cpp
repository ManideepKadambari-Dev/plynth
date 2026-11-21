#include "plynth/core/commands/WallCommands.h"

#include "plynth/core/commands/PointCommands.h"
#include "plynth/core/WallGeometry.h"
#include "plynth/core/Openings.h"

#include "CommandSupport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace plynth {

AddWallCommand::AddWallCommand(Id fromPointId, Id toPointId, double thickness, std::optional<double> height)
{
    wall_.from = std::move(fromPointId);
    wall_.to = std::move(toPointId);
    wall_.thickness = thickness;
    wall_.height = height;
}

// The id of the new wall, empty until the first apply.
const Id& AddWallCommand::wallId() const
{
    return wall_.id;
}

std::string AddWallCommand::name() const
{
    return "Add wall";
}

// Adds the wall after checking its points, with a new id on the first call and the same id on redo.
Status AddWallCommand::apply(Project& project)
{
    Floor* fromFloor = detail::floorOfPoint(project.plan, wall_.from);
    if (!fromFloor) return fail(ErrorCode::NotFound, "There is no point " + wall_.from + ".");
    Floor* toFloor = detail::floorOfPoint(project.plan, wall_.to);
    if (!toFloor) return fail(ErrorCode::NotFound, "There is no point " + wall_.to + ".");
    if (wall_.from == wall_.to) return fail(ErrorCode::InvalidValue, "A wall needs two different points.");
    if (fromFloor != toFloor) return fail(ErrorCode::InvalidValue, "The two points are on different floors.");
    if (!detail::isPositive(wall_.thickness)) {
        return fail(ErrorCode::InvalidValue, "The wall thickness must be a number above zero.");
    }
    if (wall_.height && !detail::isPositive(*wall_.height)) {
        return fail(ErrorCode::InvalidValue, "The wall height must be a number above zero.");
    }
    for (const Wall& w : fromFloor->walls) {
        const bool same = (w.from == wall_.from && w.to == wall_.to) || (w.from == wall_.to && w.to == wall_.from);
        if (same) return fail(ErrorCode::AlreadyExists, "The wall " + w.id + " already joins these two points.");
    }
    Wall w = wall_;
    if (w.id.empty()) {
        w.id = newId(project.plan, kWallIdPrefix);
    } else if (idInUse(project.plan, w.id)) {
        return fail(ErrorCode::AlreadyExists, "The id " + w.id + " is already used.");
    }
    wall_.id = w.id;
    floorId_ = fromFloor->id;
    fromFloor->walls.push_back(std::move(w));
    return {};
}

// Removes the wall that apply added.
void AddWallCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    std::erase_if(floor->walls, [&](const Wall& w) { return w.id == wall_.id; });
}

unsigned AddWallCommand::changes() const
{
    return Change::Walls;
}

Id AddWallCommand::floorId() const
{
    return floorId_;
}

RemoveWallCommand::RemoveWallCommand(Id wallId) : wallId_(std::move(wallId)) {}

std::string RemoveWallCommand::name() const
{
    return "Remove wall";
}

// Removes the wall unless a room or an opening uses it, remembering its place in the list.
Status RemoveWallCommand::apply(Project& project)
{
    Floor* floor = detail::floorOfWall(project.plan, wallId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no wall " + wallId_ + ".");
    for (const Room& r : floor->rooms) {
        if (std::find(r.walls.begin(), r.walls.end(), wallId_) != r.walls.end()) {
            return fail(ErrorCode::InUse, "The wall " + wallId_ + " is used by the room " + r.id + ".");
        }
    }
    for (const Opening& o : floor->openings) {
        if (o.wall == wallId_) return fail(ErrorCode::InUse, "The wall " + wallId_ + " has the opening " + o.id + " on it.");
    }
    const auto it = std::find_if(floor->walls.begin(), floor->walls.end(), [&](const Wall& w) { return w.id == wallId_; });
    floorId_ = floor->id;
    index_ = static_cast<std::size_t>(it - floor->walls.begin());
    removed_ = *it;
    floor->walls.erase(it);
    return {};
}

// Puts the wall back at its old place.
void RemoveWallCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    const std::size_t at = std::min(index_, floor->walls.size());
    floor->walls.insert(floor->walls.begin() + static_cast<std::ptrdiff_t>(at), removed_);
}

unsigned RemoveWallCommand::changes() const
{
    return Change::Walls;
}

Id RemoveWallCommand::floorId() const
{
    return floorId_;
}

SetWallThicknessCommand::SetWallThicknessCommand(Id wallId, double thickness, int mergeKey)
    : wallId_(std::move(wallId)), thickness_(thickness), mergeKey_(mergeKey)
{
}

std::string SetWallThicknessCommand::name() const
{
    return "Set wall thickness";
}

// Sets the thickness, remembering the old one.
Status SetWallThicknessCommand::apply(Project& project)
{
    Floor* floor = detail::floorOfWall(project.plan, wallId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no wall " + wallId_ + ".");
    if (!detail::isPositive(thickness_)) {
        return fail(ErrorCode::InvalidValue, "The wall thickness must be a number above zero.");
    }
    Wall* wall = findWall(*floor, wallId_);
    floorId_ = floor->id;
    previous_ = wall->thickness;
    wall->thickness = thickness_;
    return {};
}

// Puts the old thickness back.
void SetWallThicknessCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    if (Wall* wall = findWall(*floor, wallId_)) wall->thickness = previous_;
}

unsigned SetWallThicknessCommand::changes() const
{
    return Change::Walls;
}

Id SetWallThicknessCommand::floorId() const
{
    return floorId_;
}

// Takes over the later thickness of the same wall in the same series of changes.
bool SetWallThicknessCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetWallThicknessCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->wallId_ != wallId_) return false;
    thickness_ = other->thickness_;
    return true;
}

SplitWallCommand::SplitWallCommand(Id wallId, double offset) : wallId_(std::move(wallId)), offset_(offset) {}

// The new point, empty until the first apply.
const Id& SplitWallCommand::newPointId() const
{
    return newPointId_;
}

// The new wall, empty until the first apply.
const Id& SplitWallCommand::newWallId() const
{
    return newWallId_;
}

std::string SplitWallCommand::name() const
{
    return "Split wall";
}

// Cuts the wall at the offset: a new point, a new wall for the second half, rooms and openings updated.
Status SplitWallCommand::apply(Project& project)
{
    Floor* floor = detail::floorOfWall(project.plan, wallId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no wall " + wallId_ + ".");
    Wall* wall = findWall(*floor, wallId_);
    const WallCurve curve = wallCurve(*floor, *wall);
    if (!curve.valid) return fail(ErrorCode::NotFound, "The wall " + wallId_ + " has a missing point.");
    const double length = curve.length;
    if (!std::isfinite(offset_) || offset_ < kMinSplitDistance || offset_ > length - kMinSplitDistance) {
        return fail(ErrorCode::InvalidValue, "The split place must be inside the wall, away from both ends.");
    }
    for (const Opening& o : floor->openings) {
        if (o.wall != wallId_) continue;
        if (o.offset - o.width / 2.0 < offset_ && offset_ < o.offset + o.width / 2.0) {
            return fail(ErrorCode::InUse, "The opening " + o.id + " covers the place where the wall would be split.");
        }
    }

    const Id pointId = newPointId_.empty() ? newId(project.plan, kPointIdPrefix) : newPointId_;
    if (!newPointId_.empty() && idInUse(project.plan, pointId)) {
        return fail(ErrorCode::AlreadyExists, "The id " + pointId + " is already used.");
    }
    const Id secondId = newWallId_.empty() ? newId(project.plan, kWallIdPrefix) : newWallId_;
    if (!newWallId_.empty() && idInUse(project.plan, secondId)) {
        return fail(ErrorCode::AlreadyExists, "The id " + secondId + " is already used.");
    }

    previous_ = *floor;
    floorId_ = floor->id;
    newPointId_ = pointId;
    newWallId_ = secondId;

    const Vec2 splitAt = pointAlong(curve, offset_);
    Point middle;
    middle.id = pointId;
    middle.x = splitAt.x;
    middle.y = splitAt.y;
    const Id oldFrom = wall->from;
    const Id oldTo = wall->to;
    Wall second = *wall;
    second.id = secondId;
    second.from = pointId;
    wall->to = pointId;
    if (curve.curved) {
        // Each part stays on the same circle: its bulge is the signed sagitta of its own sweep.
        const double sign = curve.bulge > 0.0 ? 1.0 : -1.0;
        const auto partBulge = [&](double partLength) {
            const double sweep = std::fabs(curve.sweep) * partLength / curve.length;
            const double bulge = sign * curve.radius * (1.0 - std::cos(sweep / 2.0));
            return std::fabs(bulge) < kMinBulge ? 0.0 : bulge;
        };
        wall->bulge = partBulge(offset_);
        second.bulge = partBulge(length - offset_);
    }
    floor->points.push_back(middle);
    floor->walls.push_back(second);

    for (Room& r : floor->rooms) {
        const auto it = std::find(r.walls.begin(), r.walls.end(), wallId_);
        if (it == r.walls.end()) continue;
        const auto touches = [&](const Id& otherId, const Id& pointId) {
            const Wall* o = findWall(*floor, otherId);
            return o && (o->from == pointId || o->to == pointId);
        };
        bool backwards = false;
        if (it + 1 != r.walls.end()) {
            backwards = touches(*(it + 1), oldFrom) && !touches(*(it + 1), oldTo);
        }
        if (!backwards && it != r.walls.begin()) {
            backwards = touches(*(it - 1), oldTo) && !touches(*(it - 1), oldFrom);
        }
        r.walls.insert(backwards ? it : it + 1, secondId);
    }
    for (Opening& o : floor->openings) {
        if (o.wall == wallId_ && o.offset >= offset_) {
            o.wall = secondId;
            o.offset -= offset_;
        }
    }
    return {};
}

// Puts the whole floor back as it was.
void SplitWallCommand::undo(Project& project)
{
    if (Floor* floor = findFloor(project.plan, floorId_)) *floor = previous_;
}

unsigned SplitWallCommand::changes() const
{
    return Change::Points | Change::Walls | Change::Rooms | Change::Openings;
}

Id SplitWallCommand::floorId() const
{
    return floorId_;
}

// A wall end that is an existing point.
WallEnd WallEnd::atPoint(Id pointId)
{
    WallEnd e;
    e.kind = Kind::Point;
    e.id = std::move(pointId);
    return e;
}

// A wall end on an existing wall.
WallEnd WallEnd::onWall(Id wallId, double offset)
{
    WallEnd e;
    e.kind = Kind::OnWall;
    e.id = std::move(wallId);
    e.offset = offset;
    return e;
}

// A wall end at a new point.
WallEnd WallEnd::newPoint(Vec2 position)
{
    WallEnd e;
    e.kind = Kind::NewPoint;
    e.position = position;
    return e;
}

// Turns what a position snapped to into a wall end.
WallEnd wallEndFromSnap(const SnapResult& snap)
{
    switch (snap.kind) {
    case SnapKind::Point: return WallEnd::atPoint(snap.pointId);
    case SnapKind::Wall: return WallEnd::onWall(snap.wallId, snap.wallOffset);
    default: return WallEnd::newPoint(snap.position);
    }
}

DrawWallCommand::DrawWallCommand(Id floorId, WallEnd from, WallEnd to, double thickness, std::optional<double> height)
    : floorId_(std::move(floorId)), from_(std::move(from)), to_(std::move(to)), thickness_(thickness), height_(height)
{
}

// The new wall, empty until the first apply.
const Id& DrawWallCommand::wallId() const
{
    return wallId_;
}

const Id& DrawWallCommand::fromPointId() const
{
    return fromPointId_;
}

const Id& DrawWallCommand::toPointId() const
{
    return toPointId_;
}

const std::vector<Id>& DrawWallCommand::newPointIds() const
{
    return newPointIds_;
}

const std::vector<Id>& DrawWallCommand::splitWallIds() const
{
    return splitWallIds_;
}

std::string DrawWallCommand::name() const
{
    return "Draw wall";
}

namespace {

// Where a wall end lands, or an error when the end is not usable on the floor.
Status endPosition(const Floor& floor, const Plan& plan, const WallEnd& end, Vec2& out)
{
    switch (end.kind) {
    case WallEnd::Kind::Point: {
        const Point* p = findPoint(floor, end.id);
        if (!p) {
            if (const Floor* other = findFloorContaining(plan, end.id); other && other != &floor) {
                return fail(ErrorCode::InvalidValue, "The point " + end.id + " is on another floor.");
            }
            return fail(ErrorCode::NotFound, "There is no point " + end.id + ".");
        }
        out = positionOf(*p);
        return {};
    }
    case WallEnd::Kind::OnWall: {
        const Wall* w = findWall(floor, end.id);
        if (!w) {
            if (const Floor* other = findFloorContaining(plan, end.id); other && other != &floor) {
                return fail(ErrorCode::InvalidValue, "The wall " + end.id + " is on another floor.");
            }
            return fail(ErrorCode::NotFound, "There is no wall " + end.id + ".");
        }
        const WallCurve curve = wallCurve(floor, *w);
        if (!curve.valid) return fail(ErrorCode::NotFound, "The wall " + end.id + " has a missing point.");
        if (!std::isfinite(end.offset)) {
            return fail(ErrorCode::InvalidValue, "The place on the wall " + end.id + " is not a number.");
        }
        out = pointAlong(curve, end.offset);
        return {};
    }
    case WallEnd::Kind::NewPoint:
        if (!std::isfinite(end.position.x) || !std::isfinite(end.position.y)) {
            return fail(ErrorCode::InvalidValue, "The wall end position must be finite numbers.");
        }
        out = end.position;
        return {};
    }
    return fail(ErrorCode::InvalidValue, "Unknown wall end.");
}

}

// Checks both ends, then builds and applies the steps one by one, undoing them all if one fails. Redo applies the
// same steps again so every id comes out the same.
Status DrawWallCommand::apply(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");

    if (!steps_.empty()) {
        for (std::size_t i = 0; i < steps_.size(); ++i) {
            const Status s = steps_[i]->apply(project);
            if (!s.ok()) {
                for (std::size_t j = i; j-- > 0;) steps_[j]->undo(project);
                return s;
            }
        }
        return {};
    }

    Vec2 fromAt;
    Vec2 toAt;
    if (const Status s = endPosition(*floor, project.plan, from_, fromAt); !s.ok()) return s;
    if (const Status s = endPosition(*floor, project.plan, to_, toAt); !s.ok()) return s;
    if (from_.kind == WallEnd::Kind::Point && to_.kind == WallEnd::Kind::Point && from_.id == to_.id) {
        return fail(ErrorCode::InvalidValue, "A wall needs two different points.");
    }
    if (from_.kind == WallEnd::Kind::OnWall && to_.kind == WallEnd::Kind::OnWall && from_.id == to_.id) {
        return fail(ErrorCode::InvalidValue, "A wall cannot run along the wall it ends on.");
    }
    if (distance(fromAt, toAt) < kMinSplitDistance) {
        return fail(ErrorCode::InvalidValue, "The wall would have no length.");
    }
    if (!detail::isPositive(thickness_)) {
        return fail(ErrorCode::InvalidValue, "The wall thickness must be a number above zero.");
    }
    if (height_ && !detail::isPositive(*height_)) {
        return fail(ErrorCode::InvalidValue, "The wall height must be a number above zero.");
    }

    std::vector<std::unique_ptr<Command>> steps;
    std::vector<Id> newPoints;
    std::vector<Id> splitWalls;
    const auto run = [&](std::unique_ptr<Command> step) -> Status {
        const Status s = step->apply(project);
        if (s.ok()) steps.push_back(std::move(step));
        return s;
    };
    const auto makeEnd = [&](const WallEnd& end, Id& pointId) -> Status {
        switch (end.kind) {
        case WallEnd::Kind::Point:
            pointId = end.id;
            return {};
        case WallEnd::Kind::OnWall: {
            auto split = std::make_unique<SplitWallCommand>(end.id, end.offset);
            const SplitWallCommand* ref = split.get();
            if (const Status s = run(std::move(split)); !s.ok()) return s;
            pointId = ref->newPointId();
            newPoints.push_back(pointId);
            splitWalls.push_back(end.id);
            return {};
        }
        case WallEnd::Kind::NewPoint: {
            auto add = std::make_unique<AddPointCommand>(floorId_, end.position.x, end.position.y);
            const AddPointCommand* ref = add.get();
            if (const Status s = run(std::move(add)); !s.ok()) return s;
            pointId = ref->pointId();
            newPoints.push_back(pointId);
            return {};
        }
        }
        return fail(ErrorCode::InvalidValue, "Unknown wall end.");
    };

    Id fromId;
    Id toId;
    Id newWall;
    Status s = makeEnd(from_, fromId);
    if (s.ok()) s = makeEnd(to_, toId);
    if (s.ok()) {
        auto wallStep = std::make_unique<AddWallCommand>(fromId, toId, thickness_, height_);
        const AddWallCommand* ref = wallStep.get();
        s = run(std::move(wallStep));
        if (s.ok()) newWall = ref->wallId();
    }
    if (!s.ok()) {
        for (std::size_t j = steps.size(); j-- > 0;) steps[j]->undo(project);
        return s;
    }
    wallId_ = newWall;
    fromPointId_ = fromId;
    toPointId_ = toId;
    newPointIds_ = std::move(newPoints);
    splitWallIds_ = std::move(splitWalls);
    steps_ = std::move(steps);
    return {};
}

// Undoes the steps in reverse order.
void DrawWallCommand::undo(Project& project)
{
    for (std::size_t i = steps_.size(); i-- > 0;) steps_[i]->undo(project);
}

unsigned DrawWallCommand::changes() const
{
    return Change::Points | Change::Walls | Change::Rooms | Change::Openings;
}

Id DrawWallCommand::floorId() const
{
    return floorId_;
}

}

namespace plynth {

SetWallBulgeCommand::SetWallBulgeCommand(Id wallId, double bulge, int mergeKey)
    : wallId_(std::move(wallId)), bulge_(bulge), mergeKey_(mergeKey)
{
}

std::string SetWallBulgeCommand::name() const
{
    return "Bend wall";
}

// Sets the bulge after checking it gives a real arc, remembering the old one.
Status SetWallBulgeCommand::apply(Project& project)
{
    Floor* floor = detail::floorOfWall(project.plan, wallId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no wall " + wallId_ + ".");
    Wall* wall = findWall(*floor, wallId_);
    if (!std::isfinite(bulge_)) return fail(ErrorCode::InvalidValue, "The bend must be a finite number.");
    const WallCurve straight = wallCurve(*floor, *wall);
    if (!straight.valid) return fail(ErrorCode::NotFound, "The wall " + wallId_ + " has a missing point.");
    const double size = std::fabs(bulge_);
    const double stored = size < kMinBulge ? 0.0 : bulge_;
    if (stored != 0.0) {
        const double limit = kMaxBulgeRatio * straight.chord;
        if (size > limit + 1e-9) {
            char text[32];
            std::snprintf(text, sizeof text, "%.2f", limit);
            return fail(ErrorCode::InvalidValue,
                        std::string("The bend is too large for this wall. The most is a half circle, a bend of ") + text + " m.");
        }
        const double radius = (straight.chord * straight.chord / 4.0 + size * size) / (2.0 * size);
        if (!(radius > wall->thickness / 2.0)) {
            return fail(ErrorCode::InvalidValue, "The bend is too tight for the thickness of this wall.");
        }
    }
    floorId_ = floor->id;
    previous_ = wall->bulge;
    wall->bulge = stored;
    return {};
}

// Puts the old bulge back.
void SetWallBulgeCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    if (Wall* wall = findWall(*floor, wallId_)) wall->bulge = previous_;
}

unsigned SetWallBulgeCommand::changes() const
{
    return Change::Walls;
}

Id SetWallBulgeCommand::floorId() const
{
    return floorId_;
}

// Takes over the later bulge of the same wall in the same drag.
bool SetWallBulgeCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetWallBulgeCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->wallId_ != wallId_) return false;
    bulge_ = other->bulge_;
    return true;
}

}

namespace plynth {

SetWallHeightCommand::SetWallHeightCommand(Id wallId, std::optional<double> height, int mergeKey)
    : wallId_(std::move(wallId)), height_(height), mergeKey_(mergeKey)
{
}

std::string SetWallHeightCommand::name() const
{
    return height_ ? "Set wall height" : "Clear wall height";
}

// Sets or clears the wall's own height unless an opening on the wall would end up above it.
Status SetWallHeightCommand::apply(Project& project)
{
    Floor* floor = detail::floorOfWall(project.plan, wallId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no wall " + wallId_ + ".");
    if (height_ && !detail::isPositive(*height_)) {
        return fail(ErrorCode::InvalidValue, "The wall height must be a number above zero.");
    }
    Wall* wall = findWall(*floor, wallId_);
    const double top = height_ ? *height_ : floor->ceilingHeight;
    for (const Opening& o : floor->openings) {
        if (o.wall != wallId_) continue;
        if (o.sill + o.height > top + kOpeningTouchTolerance) {
            char text[48];
            std::snprintf(text, sizeof text, "%.2f m", top);
            return fail(ErrorCode::InvalidValue,
                        "The opening " + o.id + " would reach above the wall, which would be " + text + " high.");
        }
    }
    floorId_ = floor->id;
    previous_ = wall->height;
    wall->height = height_;
    return {};
}

// Puts the old height back.
void SetWallHeightCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    if (Wall* wall = findWall(*floor, wallId_)) wall->height = previous_;
}

unsigned SetWallHeightCommand::changes() const
{
    return Change::Walls;
}

Id SetWallHeightCommand::floorId() const
{
    return floorId_;
}

// Takes over the later height of the same wall in the same series of changes.
bool SetWallHeightCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetWallHeightCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->wallId_ != wallId_) return false;
    height_ = other->height_;
    return true;
}

}
