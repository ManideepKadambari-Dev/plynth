#pragma once

#include "../Command.h"

#include <cstddef>
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
// its own key. 0 never merges.
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
