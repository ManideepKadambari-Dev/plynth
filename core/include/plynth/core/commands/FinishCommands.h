#pragma once

#include "../Command.h"

#include <optional>
#include <string>

namespace plynth {

// Replaces the project's trim settings. InvalidValue for what checkTrimSettings refuses.
class SetTrimCommand : public Command {
public:
    explicit SetTrimCommand(TrimSettings trim);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Settings.
    unsigned changes() const override;

private:
    TrimSettings trim_;
    TrimSettings previous_;
};

// Replaces the project's variation settings: seed and switches.
class SetVariationCommand : public Command {
public:
    explicit SetVariationCommand(VariationSettings variation);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Settings.
    unsigned changes() const override;

private:
    VariationSettings variation_;
    VariationSettings previous_;
};

// Turns set dressing on or off for a room. NotFound for an unknown room.
class SetRoomDressingCommand : public Command {
public:
    SetRoomDressingCommand(Id roomId, bool dressing);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Rooms.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id roomId_;
    bool dressing_;
    Id floorId_;
    bool previous_ = true;
};

// Sets which door or window an opening gets: a named catalog asset (frame), or none with autoPick saying
// whether the build picks a fitting one (true) or builds its own fill (false). NotFound for an unknown opening,
// InvalidValue for an asset that is not a well formed name.
class SetOpeningFrameCommand : public Command {
public:
    SetOpeningFrameCommand(Id openingId, std::optional<std::string> asset, bool autoPick);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Openings.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id openingId_;
    std::optional<std::string> asset_;
    bool autoPick_;
    Id floorId_;
    std::optional<std::string> previousFrame_;
    bool previousAuto_ = true;
};

}
