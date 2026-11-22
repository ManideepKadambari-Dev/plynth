#pragma once

#include "../Command.h"

#include <cstddef>
#include <optional>
#include <string>

namespace plynth {

// Sets the plan's roof default, used by every roof section without an entry of its own. InvalidValue for what
// checkRoofSettings (Roofs.h) refuses.
class SetRoofDefaultCommand : public Command {
public:
    explicit SetRoofDefaultCommand(RoofSettings settings);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Roofs.
    unsigned changes() const override;

private:
    RoofSettings settings_;
    RoofSettings previous_;
};

// Gives the roof section of a floor that holds (x, y) settings of its own: adds a RoofSection entry anchored
// there (id newId with kRoofSectionIdPrefix, the same on redo). NotFound for an unknown floor, InvalidValue
// "There is no roof section there." when no section of that floor holds the point, AlreadyExists naming the
// entry when one already governs that section, and whatever checkRoofSettings refuses or a malformed material.
class AddRoofSectionCommand : public Command {
public:
    AddRoofSectionCommand(Id floorId, double x, double y, RoofSettings settings,
                          std::optional<std::string> material = {});

    // The id the entry got; empty before the first apply.
    const Id& newSectionId() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Roofs.
    unsigned changes() const override;
    Id floorId() const override;

private:
    RoofSection section_;
};

// Replaces the settings and material of a roof section entry (its id, floor and anchor stay). NotFound for an
// unknown entry, and whatever checkRoofSettings refuses or a malformed material.
class SetRoofSectionCommand : public Command {
public:
    SetRoofSectionCommand(Id sectionId, RoofSettings settings, std::optional<std::string> material);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Roofs.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id sectionId_;
    RoofSettings settings_;
    std::optional<std::string> material_;
    RoofSection previous_;
};

// Removes a roof section entry, so its section goes back to the plan's default. NotFound for an unknown entry.
class RemoveRoofSectionCommand : public Command {
public:
    explicit RemoveRoofSectionCommand(Id sectionId);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Roofs.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id sectionId_;
    RoofSection removed_;
    std::size_t index_ = 0;
};

}
