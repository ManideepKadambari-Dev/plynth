#pragma once

#include "../Command.h"
#include "../MaterialDefaults.h"
#include "../Materials.h"

#include <optional>
#include <string>

namespace plynth {

// The material commands. None of them opens the library: the app and the CLI check the material with
// materialForUse (catalog/MaterialCatalog.h) before they run one. Empty material clears the setting.
// InvalidValue for a material that is not a well formed name (the plan's name rule).

// Sets or clears the inside or outside material of a wall. NotFound for an unknown wall. Changes Walls.
class SetWallMaterialCommand : public Command {
public:
    SetWallMaterialCommand(Id wallId, WallFace face, std::optional<std::string> material);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id wallId_;
    Id floorId_;
    WallFace face_;
    std::optional<std::string> material_;
    std::optional<std::string> previous_;
};

// Sets or clears the floor, wall or ceiling material of a room. NotFound for an unknown room. Changes Rooms.
class SetRoomMaterialCommand : public Command {
public:
    SetRoomMaterialCommand(Id roomId, RoomSurface surface, std::optional<std::string> material);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id roomId_;
    Id floorId_;
    RoomSurface surface_;
    std::optional<std::string> material_;
    std::optional<std::string> previous_;
};

// Sets or clears the project's default material for one surface. Changes Settings.
class SetDefaultMaterialCommand : public Command {
public:
    SetDefaultMaterialCommand(Surface surface, std::optional<std::string> material);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;

private:
    Surface surface_;
    std::optional<std::string> material_;
    std::optional<std::string> previous_;
};

}
