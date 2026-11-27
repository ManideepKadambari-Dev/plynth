#pragma once

#include "../Command.h"

#include <cstddef>
#include <string>

namespace plynth {

// Light strips (LightStrips.h).

// Adds a light strip to a floor (its id is ignored; newId with kLightStripIdPrefix, the same on redo). NotFound
// for an unknown floor, and whatever checkLightStrip refuses.
class AddLightStripCommand : public Command {
public:
    AddLightStripCommand(Id floorId, LightStrip strip);

    const Id& newStripId() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Lights.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id floorId_;
    LightStrip strip_;
};

// Replaces every value of a strip except its id. Changes with the same non zero mergeKey and strip merge into
// one undo step, for a drag. NotFound, checkLightStrip.
class SetLightStripCommand : public Command {
public:
    SetLightStripCommand(Id stripId, LightStrip values, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Lights.
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id stripId_;
    LightStrip values_;
    int mergeKey_;
    Id floorId_;
    LightStrip previous_;
};

// Removes a strip. NotFound. A stair that is removed also removes the step strips that name it, in the same
// undo step (RemoveStairCommand), and a delete (makeDeleteCommand) removes named strips before stairs.
class RemoveLightStripCommand : public Command {
public:
    explicit RemoveLightStripCommand(Id stripId);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Lights.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id stripId_;
    Id floorId_;
    LightStrip removed_;
    std::size_t index_ = 0;
};

}
