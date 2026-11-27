#pragma once

#include "../Command.h"

#include <cstddef>
#include <string>
#include <vector>

namespace plynth {

// Adds a stair to a floor with the values of stair (its id is ignored; the new one is newId with
// kStairIdPrefix, the same on redo). NotFound for an unknown floor. InvalidValue "Add a floor above before
// placing a stair." when the floor is the top one, and whatever checkStair (Stairs.h) refuses.
class AddStairCommand : public Command {
public:
    AddStairCommand(Id floorId, Stair stair);

    // The id the stair got; empty before the first apply.
    const Id& newStairId() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Stairs.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id floorId_;
    Stair stair_;
};

// Moves and turns a stair. InvalidValue for a value that is not finite (rotation is normalized), NotFound for
// an unknown stair. Changes with the same non zero mergeKey and stair merge into one undo step, for a drag.
class MoveStairCommand : public Command {
public:
    MoveStairCommand(Id stairId, double x, double y, double rotation, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Stairs.
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id stairId_;
    double x_;
    double y_;
    double rotation_;
    int mergeKey_;
    Id floorId_;
    Stair previous_;
};

// Replaces every value of a stair except its id (type, position, rotation, width, turn, material). NotFound for
// an unknown stair, and whatever checkStair refuses.
class SetStairCommand : public Command {
public:
    SetStairCommand(Id stairId, Stair values);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Stairs.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id stairId_;
    Stair values_;
    Id floorId_;
    Stair previous_;
};

// Removes a stair, in the same undo step the step light strips that name it (LightStrips.h), and clears the
// stair of any other strip that names it. NotFound for an unknown stair.
class RemoveStairCommand : public Command {
public:
    explicit RemoveStairCommand(Id stairId);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Stairs, and Lights when step strips went with it.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id stairId_;
    Id floorId_;
    Stair removed_;
    std::size_t index_ = 0;
    bool stripsChanged_ = false;
    std::vector<LightStrip> previousStrips_; // the floor's light strips before, when stripsChanged_
};

}
