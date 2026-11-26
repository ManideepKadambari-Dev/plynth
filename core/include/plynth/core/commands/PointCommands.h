#pragma once

#include "../Command.h"

#include <cstddef>
#include <memory>
#include <string>

namespace plynth {

// Adds a point to a floor, at the end of its point list. The new id comes from newId on the first apply
// and is kept for redo. NotFound for an unknown floor, InvalidValue for a coordinate that is not finite
// or a negative corner radius.
class AddPointCommand : public Command {
public:
    AddPointCommand(Id floorId, double x, double y, double cornerRadius = 0.0);

    // The id of the new point. Empty until apply has succeeded once.
    const Id& pointId() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id floorId_;
    Point point_;
};

// Moves a point. NotFound for an unknown point, InvalidValue for a coordinate that is not finite.
// Moves with the same non zero mergeKey and point merge into one undo step, so the app gives each drag
// its own key. 0 never merges. A JoinPointCommand (WallCommands.h) of the same point and key merges in too, so a
// drag that ends by joining the point to another point or a wall is one undo step; no move merges after it.
class MovePointCommand : public Command {
public:
    MovePointCommand(Id pointId, double x, double y, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id pointId_;
    Id floorId_;
    double x_;
    double y_;
    double oldX_ = 0.0;
    double oldY_ = 0.0;
    int mergeKey_;
    std::shared_ptr<Command> join_; // the join that ended the drag, applied after the move
};

// Sets the corner radius of a point (DESIGN.md section 7.2). The radius is stored as given; cornerRounding
// in WallGeometry.h says whether and how much it rounds. NotFound for an unknown point, InvalidValue when the
// radius is not finite or below zero. Openings near the corner may stop fitting; openingProblems lists them.
// Changes with the same non zero mergeKey and point merge into one undo step.
class SetCornerRadiusCommand : public Command {
public:
    SetCornerRadiusCommand(Id pointId, double radius, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Points.
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id pointId_;
    double radius_;
    int mergeKey_;
    Id floorId_;
    double previous_ = 0.0;
};

// Removes a point. NotFound for an unknown point, InUse while a wall uses it.
// Undo puts it back at the same place in the list.
class RemovePointCommand : public Command {
public:
    explicit RemovePointCommand(Id pointId);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id pointId_;
    Id floorId_;
    Point removed_;
    std::size_t index_ = 0;
};

}
