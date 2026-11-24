#include "plynth/core/commands/FinishCommands.h"

#include "commands/CommandSupport.h"

#include <utility>

namespace plynth {

SetTrimCommand::SetTrimCommand(TrimSettings trim) : trim_(std::move(trim)) {}

std::string SetTrimCommand::name() const
{
    return "Change trim";
}

// Checks the trim settings and replaces them.
Status SetTrimCommand::apply(Project& project)
{
    if (const Status s = checkTrimSettings(trim_); !s.ok()) return s;
    previous_ = project.settings.trim;
    project.settings.trim = trim_;
    return {};
}

// Puts the old trim settings back.
void SetTrimCommand::undo(Project& project)
{
    project.settings.trim = previous_;
}

unsigned SetTrimCommand::changes() const
{
    return Change::Settings;
}

SetVariationCommand::SetVariationCommand(VariationSettings variation) : variation_(variation) {}

std::string SetVariationCommand::name() const
{
    return "Change variation";
}

// Replaces the variation settings.
Status SetVariationCommand::apply(Project& project)
{
    previous_ = project.settings.variation;
    project.settings.variation = variation_;
    return {};
}

// Puts the old variation settings back.
void SetVariationCommand::undo(Project& project)
{
    project.settings.variation = previous_;
}

unsigned SetVariationCommand::changes() const
{
    return Change::Settings;
}

SetRoomDressingCommand::SetRoomDressingCommand(Id roomId, bool dressing) : roomId_(std::move(roomId)), dressing_(dressing) {}

std::string SetRoomDressingCommand::name() const
{
    return dressing_ ? "Turn set dressing on" : "Turn set dressing off";
}

// Sets the room's dressing switch.
Status SetRoomDressingCommand::apply(Project& project)
{
    Floor* floor = findFloorContaining(project.plan, roomId_);
    Room* room = floor ? findRoom(*floor, roomId_) : nullptr;
    if (!room) return fail(ErrorCode::NotFound, "There is no room " + roomId_ + ".");
    floorId_ = floor->id;
    previous_ = room->dressing;
    room->dressing = dressing_;
    return {};
}

// Puts the old switch back.
void SetRoomDressingCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (Room* room = floor ? findRoom(*floor, roomId_) : nullptr) room->dressing = previous_;
}

unsigned SetRoomDressingCommand::changes() const
{
    return Change::Rooms;
}

Id SetRoomDressingCommand::floorId() const
{
    return floorId_;
}

SetOpeningFrameCommand::SetOpeningFrameCommand(Id openingId, std::optional<std::string> asset, bool autoPick)
    : openingId_(std::move(openingId)), asset_(std::move(asset)), autoPick_(autoPick)
{
}

std::string SetOpeningFrameCommand::name() const
{
    return "Change door or window asset";
}

// Checks the asset name and sets the opening's frame and picking switch.
Status SetOpeningFrameCommand::apply(Project& project)
{
    Floor* floor = findFloorContaining(project.plan, openingId_);
    Opening* opening = floor ? findOpening(*floor, openingId_) : nullptr;
    if (!opening) return fail(ErrorCode::NotFound, "There is no opening " + openingId_ + ".");
    if (asset_ && !detail::isWellFormedName(*asset_)) {
        return fail(ErrorCode::InvalidValue, "The asset name may only use letters, digits, \"_\" and \"-\".");
    }
    floorId_ = floor->id;
    previousFrame_ = opening->frame;
    previousAuto_ = opening->autoFrame;
    opening->frame = asset_;
    opening->autoFrame = autoPick_;
    return {};
}

// Puts the old frame and switch back.
void SetOpeningFrameCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (Opening* opening = floor ? findOpening(*floor, openingId_) : nullptr) {
        opening->frame = previousFrame_;
        opening->autoFrame = previousAuto_;
    }
}

unsigned SetOpeningFrameCommand::changes() const
{
    return Change::Openings;
}

Id SetOpeningFrameCommand::floorId() const
{
    return floorId_;
}

}
