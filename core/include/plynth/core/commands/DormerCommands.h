#pragma once

#include "../Command.h"

#include <cstddef>
#include <string>

namespace plynth {

// Dormers (Dormers.h). A dormer that does not fit its roof is refused: commands validate before changing
// anything (DESIGN.md section 7.18).

// Adds a dormer to a floor (its id is ignored; the new one is newId with kDormerIdPrefix, the same on redo).
// NotFound for an unknown floor; checkDormer; InvalidValue with dormerPlacement's sentence when it does not
// stand on a pitched roof of the floor or does not fit there, and InvalidValue when it would overlap another
// dormer of the floor that fits.
class AddDormerCommand : public Command {
public:
    AddDormerCommand(Id floorId, Dormer dormer);

    const Id& newDormerId() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Roofs.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id floorId_;
    Dormer dormer_;
};

// Replaces every value of a dormer except its id, with the same checks (its own old place does not count as an
// overlap). NotFound for an unknown dormer. Changes of the same dormer with the same non zero mergeKey merge
// into one undo step, for a drag.
class SetDormerCommand : public Command {
public:
    SetDormerCommand(Id dormerId, Dormer values, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Roofs.
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id dormerId_;
    Dormer values_;
    int mergeKey_;
    Id floorId_;
    Dormer previous_;
};

// Removes a dormer. NotFound for an unknown dormer. A dormer whose roof section goes away is not removed: it stays
// in the plan and the build reports dormer_not_on_roof.
class RemoveDormerCommand : public Command {
public:
    explicit RemoveDormerCommand(Id dormerId);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Roofs.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id dormerId_;
    Id floorId_;
    Dormer removed_;
    std::size_t index_ = 0;
};

}
