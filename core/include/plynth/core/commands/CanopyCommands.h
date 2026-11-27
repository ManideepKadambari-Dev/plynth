#pragma once

#include "../Command.h"

#include <cstddef>
#include <string>

namespace plynth {

// Canopies (Canopies.h).

// Adds a canopy to a floor (its id is ignored; the new one is newId with kCanopyIdPrefix, the same on redo).
// NotFound for an unknown floor, and whatever checkCanopy refuses.
class AddCanopyCommand : public Command {
public:
    AddCanopyCommand(Id floorId, Canopy canopy);

    const Id& newCanopyId() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Outdoor.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id floorId_;
    Canopy canopy_;
};

// Replaces every value of a canopy except its id. Same mergeKey rule as SetDeckCommand. NotFound, checkCanopy.
class SetCanopyCommand : public Command {
public:
    SetCanopyCommand(Id canopyId, Canopy values, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Outdoor.
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id canopyId_;
    Canopy values_;
    int mergeKey_;
    Id floorId_;
    Canopy previous_;
};

// Removes a canopy. NotFound for an unknown canopy.
class RemoveCanopyCommand : public Command {
public:
    explicit RemoveCanopyCommand(Id canopyId);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Outdoor.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id canopyId_;
    Id floorId_;
    Canopy removed_;
    std::size_t index_ = 0;
};

}
