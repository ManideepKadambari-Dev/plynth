#pragma once

#include "../Command.h"

#include <cstddef>
#include <optional>
#include <string>

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

}
