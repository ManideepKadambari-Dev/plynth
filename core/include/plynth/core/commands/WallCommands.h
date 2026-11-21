#pragma once

#include "../Command.h"
#include "../Geometry.h"
#include "../Snapping.h"

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

// Removes a wall. NotFound for an unknown wall, InUse while a room lists it or an opening sits on it.
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

// Splits a straight wall in two at a new point, offset metres along it from its from point.
// The wall keeps its id and runs from its from point to the new point; a new wall with a new id runs from
// the new point to the old to point, copying thickness, height and materials. Rooms that list the wall get
// the new wall listed next to it so the ring stays in order: right after it when the ring runs along the
// wall from its from point to its to point, right before it when the ring runs the other way.
// Openings past the split move to the new wall with their offset reduced.
// NotFound for an unknown wall. InvalidValue when the offset is not finite or not at least kMinSplitDistance
// from both ends, or the wall is curved (bulge not 0). InUse when an opening covers the split place.
class SplitWallCommand : public Command {
public:
    SplitWallCommand(Id wallId, double offset);

    // The new point and the new wall. Empty until apply has succeeded once.
    const Id& newPointId() const;
    const Id& newWallId() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Points, Walls, Rooms and Openings.
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
// then adds the wall between them, as AddWallCommand does. The ids picked on the first apply are kept for redo.
// NotFound for an unknown floor, point or wall. InvalidValue when an end is on another floor, a position is not
// finite, both ends come to the same point, both ends are on the same wall (a wall cannot run along another
// one), or as for SplitWallCommand and AddWallCommand. AlreadyExists when a wall already joins the two ends.
class DrawWallCommand : public Command {
public:
    DrawWallCommand(Id floorId, WallEnd from, WallEnd to, double thickness = kDefaultWallThickness,
                    std::optional<double> height = {});

    // Ids after apply has succeeded once. Empty before.
    const Id& wallId() const;
    const Id& fromPointId() const;
    const Id& toPointId() const;
    // The points this command added and the walls it split, in the order it made them.
    const std::vector<Id>& newPointIds() const;
    const std::vector<Id>& splitWallIds() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Points, Walls, Rooms and Openings.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id floorId_;
    WallEnd from_;
    WallEnd to_;
    double thickness_;
    std::optional<double> height_;
    Id wallId_;
    Id fromPointId_;
    Id toPointId_;
    std::vector<Id> newPointIds_;
    std::vector<Id> splitWallIds_;
    std::vector<std::unique_ptr<Command>> steps_; // made on the first apply, applied again for redo
};

}
