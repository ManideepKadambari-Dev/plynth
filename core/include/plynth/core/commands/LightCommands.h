#pragma once

#include "../Command.h"

#include <cstddef>
#include <string>

namespace plynth {

// Light markers (Plan.h). Every command keeps a light's room the room its position is in (roomAt), or none.

// Adds a light marker to a floor with the values of light (its id is ignored; the new one is newId with
// kLightIdPrefix, the same on redo). NotFound for an unknown floor. InvalidValue when x or y is not finite or z
// is not from 0 to the floor's ceiling height.
class AddLightCommand : public Command {
public:
    AddLightCommand(Id floorId, LightMarker light);

    // The id the light got; empty before the first apply.
    const Id& newLightId() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Lights.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id floorId_;
    LightMarker light_;
};

// Moves a light on the plan. InvalidValue for a value that is not finite, NotFound for an unknown light.
// Changes with the same non zero mergeKey and light merge into one undo step, for a drag.
class MoveLightCommand : public Command {
public:
    MoveLightCommand(Id lightId, double x, double y, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Lights.
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id lightId_;
    double x_;
    double y_;
    int mergeKey_;
    Id floorId_;
    LightMarker previous_;
};

// Sets a light's kind and height. NotFound for an unknown light, InvalidValue for a height that is not from 0
// to the floor's ceiling height.
class SetLightCommand : public Command {
public:
    SetLightCommand(Id lightId, LightKind kind, double z);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Lights.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id lightId_;
    LightKind kind_;
    double z_;
    Id floorId_;
    LightMarker previous_;
};

// Removes a light. NotFound for an unknown light.
class RemoveLightCommand : public Command {
public:
    explicit RemoveLightCommand(Id lightId);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Lights.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id lightId_;
    Id floorId_;
    LightMarker removed_;
    std::size_t index_ = 0;
};

}
