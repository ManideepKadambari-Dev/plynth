#pragma once

#include "../Command.h"
#include "../Geometry.h"
#include "../Snapping.h"
#include "RoomCommands.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace plynth {

// Adds a straight wall between two points, on the floor that holds them. The new id comes from newId on
// the first apply and is kept for redo. NotFound for an unknown point, InvalidValue when the points are
// the same, on different floors, or thickness or height is not finite and above zero, AlreadyExists when
// a wall already joins the two points in either direction.
class AddWallCommand : public Command {
public:
    AddWallCommand(Id fromPointId, Id toPointId, double thickness = kDefaultWallThickness,
                   std::optional<double> height = {});

    // The id of the new wall. Empty until apply has succeeded once.
    const Id& wallId() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id floorId_;
    Wall wall_;
};

// Removes a wall. NotFound for an unknown wall, InUse while a room lists it, an opening sits on it or an item
// is mounted on it.
// Undo puts it back at the same place in the list.
class RemoveWallCommand : public Command {
public:
    explicit RemoveWallCommand(Id wallId);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id wallId_;
    Id floorId_;
    Wall removed_;
    std::size_t index_ = 0;
};

// A split point must be at least this far from both ends of the wall, in metres.
inline constexpr double kMinSplitDistance = 0.001;

// Changes the thickness of a wall. NotFound for an unknown wall, InvalidValue unless finite and above zero.
// Changes with the same non zero mergeKey and wall merge into one undo step, for a spin box being stepped.
class SetWallThicknessCommand : public Command {
public:
    SetWallThicknessCommand(Id wallId, double thickness, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id wallId_;
    Id floorId_;
    double thickness_;
    double previous_ = 0.0;
    int mergeKey_;
};

// Splits a wall in two at a new point, offset metres along its true centre line from its from point.
// The wall keeps its id and runs from its from point to the new point; a new wall with a new id runs from
// the new point to the old to point, copying thickness, height and materials. A curved wall is split on its
// own circle: the new point is on the arc and each part gets the bulge that keeps it on that circle, the
// signed sagitta of its own sweep, radius * (1 - cos(sweep / 2)). Rooms that list the wall get
// the new wall listed next to it so the ring stays in order: right after it when the ring runs along the
// wall from its from point to its to point, right before it when the ring runs the other way.
// Openings past the split move to the new wall with their offset reduced. Wall items whose centre is past the
// split (wallItemSpot offset) move to the new wall; their position does not change.
// NotFound for an unknown wall. InvalidValue when the offset is not finite or not at least kMinSplitDistance
// from both ends. InUse when an opening covers the split place.
class SplitWallCommand : public Command {
public:
    SplitWallCommand(Id wallId, double offset);

    // The new point and the new wall. Empty until apply has succeeded once.
    const Id& newPointId() const;
    const Id& newWallId() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Points, Walls, Rooms, Openings and Items.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id wallId_;
    double offset_;
    Id floorId_;
    Id newPointId_;
    Id newWallId_;
    Floor previous_; // the floor before the split, for an exact undo
};

// Sets or clears a wall's own height, for half walls. Empty uses the floor's ceiling height.
// NotFound for an unknown wall. InvalidValue when a height is not finite and above zero, or when an opening
// on the wall would then reach above it (sill plus height above the new height; the message names it).
// Changes with the same non zero mergeKey and wall merge into one undo step.
class SetWallHeightCommand : public Command {
public:
    SetWallHeightCommand(Id wallId, std::optional<double> height, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Walls.
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id wallId_;
    std::optional<double> height_;
    int mergeKey_;
    Id floorId_;
    std::optional<double> previous_;
};

// Bends a wall into an arc or straightens it (DESIGN.md section 7.2). bulge as in Plan.h; a bulge smaller
// than kMinBulge either way is stored as 0. NotFound for an unknown wall. InvalidValue when bulge is not
// finite, is larger either way than kMaxBulgeRatio times the straight distance between the wall's points,
// or would give an arc with a radius not above half the wall's thickness. Openings keep their offsets and
// may stop fitting; openingProblems lists them. Changes with the same non zero mergeKey and wall merge into
// one undo step, so dragging the wall's middle is one step.
class SetWallBulgeCommand : public Command {
public:
    SetWallBulgeCommand(Id wallId, double bulge, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Walls.
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id wallId_;
    double bulge_;
    int mergeKey_;
    Id floorId_;
    double previous_ = 0.0;
};

// One end of a wall being drawn: an existing point, a place on an existing wall (which gets split there),
// or a new point.
struct WallEnd {
    enum class Kind { Point, OnWall, NewPoint };

    Kind kind = Kind::NewPoint;
    Id id;               // Point: the point. OnWall: the wall to split
    double offset = 0.0; // OnWall: distance along the wall from its from point
    Vec2 position;       // NewPoint: where the new point goes

    static WallEnd atPoint(Id pointId);
    static WallEnd onWall(Id wallId, double offset);
    static WallEnd newPoint(Vec2 position);
};

// The wall end a snapped position stands for: the point it snapped to, the wall it snapped to, or a new
// point at the snapped position. The app and the CLI both use this, so a click and a command agree.
WallEnd wallEndFromSnap(const SnapResult& snap);

// Draws one wall in a single undo step: makes each end (adding a point or splitting a wall as needed),
// then adds the wall between them, as AddWallCommand does, then splits the rooms the new wall cuts in two
// (SplitRoomsByWallCommand). The ids picked on the first apply are kept for redo.
// thickness, when given, is used as it is. When empty the new wall takes the thickness of the wall it joins at its
// start (the wall it ends on, or the first wall of that point in plan order), else at its end, else
// unjoinedThickness, so a wall drawn on from an existing one matches it.
// NotFound for an unknown floor, point or wall. InvalidValue when an end is on another floor, a position is not
// finite, both ends come to the same point, both ends are on the same wall (a wall cannot run along another
// one), or as for SplitWallCommand and AddWallCommand. AlreadyExists when a wall already joins the two ends.
class DrawWallCommand : public Command {
public:
    DrawWallCommand(Id floorId, WallEnd from, WallEnd to, std::optional<double> thickness = {},
                    std::optional<double> height = {}, double unjoinedThickness = kDefaultWallThickness);

    // Ids after apply has succeeded once. Empty before.
    const Id& wallId() const;
    const Id& fromPointId() const;
    const Id& toPointId() const;
    // The points this command added and the walls it split, in the order it made them.
    const std::vector<Id>& newPointIds() const;
    const std::vector<Id>& splitWallIds() const;
    // The rooms made by splitting a room the new wall cuts in two.
    const std::vector<Id>& newRoomIds() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Points, Walls, Rooms, Openings, Items and Lights.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id floorId_;
    WallEnd from_;
    WallEnd to_;
    std::optional<double> thickness_;
    std::optional<double> height_;
    double unjoinedThickness_;
    Id wallId_;
    Id fromPointId_;
    Id toPointId_;
    std::vector<Id> newPointIds_;
    std::vector<Id> splitWallIds_;
    std::vector<Id> newRoomIds_;
    std::vector<std::unique_ptr<Command>> steps_; // made on the first apply, applied again for redo
};

// The thickness of the wall a wall end joins: the wall it is on, or the first wall of its point in plan order.
// Empty for a new point, a point no wall uses, or an unknown id.
std::optional<double> joinedThickness(const Floor& floor, const WallEnd& end);

// Joins a point onto another point or onto a place on a wall, for a dragged point dropped there. A place on a wall
// is split first, as SplitWallCommand does. The walls of the point then run to the other point instead and the
// point is removed. Those walls take the thickness of the wall they join (joinedThickness of the target), except
// a curved one that the new thickness would bend too tightly, which keeps its own. Rooms that a moved wall now cuts
// in two are split (SplitRoomsByWallCommand). Undo puts the floor back exactly as it was.
// NotFound for an unknown point or wall. InvalidValue when the target is a new point, on another floor, the point
// itself, a wall that ends at the point, or a point a wall of the point already runs to, or when a curved wall
// would no longer be a real arc. AlreadyExists when a wall of the point would then join the same two points as
// another wall. A MovePointCommand of the same point and the same non zero mergeKey takes this command in, so a
// drag that ends in a join is one undo step.
class JoinPointCommand : public Command {
public:
    JoinPointCommand(Id pointId, WallEnd target, int mergeKey = 0);

    const Id& pointId() const;
    // The point the walls now run to. Empty until apply has succeeded once.
    const Id& targetPointId() const;
    int mergeKey() const;
    // The rooms made by splitting rooms that a moved wall cuts in two.
    std::vector<Id> newRoomIds() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Points, Walls, Rooms, Openings, Items and Lights.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id pointId_;
    WallEnd target_;
    int mergeKey_;
    Id floorId_;
    Id targetPointId_;
    std::optional<SplitWallCommand> split_;
    std::vector<SplitRoomsByWallCommand> roomSplits_;
    Floor previous_; // the floor before the join, for an exact undo
};

}
