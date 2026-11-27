#pragma once

#include "../Command.h"

#include <cstddef>
#include <string>

namespace plynth {

// Railings drawn on the plan (Railings.h).

// Adds a railing to a floor (its id is ignored; the new one is newId with kRailingIdPrefix, the same on redo).
// NotFound for an unknown floor or host deck, and whatever checkRailing refuses.
class AddRailingCommand : public Command {
public:
    AddRailingCommand(Id floorId, Railing railing);

    const Id& newRailingId() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Outdoor.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id floorId_;
    Railing railing_;
};

// Replaces every value of a railing except its id. Same mergeKey rule as SetDeckCommand. NotFound, checkRailing.
class SetRailingCommand : public Command {
public:
    SetRailingCommand(Id railingId, Railing values, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Outdoor.
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id railingId_;
    Railing values_;
    int mergeKey_;
    Id floorId_;
    Railing previous_;
};

// Removes a railing. NotFound for an unknown railing.
class RemoveRailingCommand : public Command {
public:
    explicit RemoveRailingCommand(Id railingId);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Outdoor.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id railingId_;
    Id floorId_;
    Railing removed_;
    std::size_t index_ = 0;
};

}
