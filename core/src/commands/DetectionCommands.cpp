#include "plynth/core/commands/DetectionCommands.h"

#include "plynth/core/Snapping.h"
#include "plynth/core/commands/RoomCommands.h"
#include "plynth/core/commands/WallCommands.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace plynth {

namespace {

constexpr double kMinSnapRadius = 0.05; // metres

// Adds an id to a list unless the list already has it.
void addOnce(std::vector<Id>& list, const Id& id)
{
    if (std::find(list.begin(), list.end(), id) == list.end()) list.push_back(id);
}

}

ApplyDetectionCommand::ApplyDetectionCommand(Id floorId, DetectionResult proposals, bool addRooms)
    : floorId_(std::move(floorId)), proposals_(std::move(proposals)), addRooms_(addRooms)
{
}

const std::vector<Id>& ApplyDetectionCommand::wallIds() const
{
    return wallIds_;
}

const std::vector<Id>& ApplyDetectionCommand::pointIds() const
{
    return pointIds_;
}

const std::vector<Id>& ApplyDetectionCommand::splitWallIds() const
{
    return splitWallIds_;
}

const std::vector<Id>& ApplyDetectionCommand::roomIds() const
{
    return roomIds_;
}

std::size_t ApplyDetectionCommand::skippedWalls() const
{
    return skippedWalls_;
}

std::size_t ApplyDetectionCommand::skippedRooms() const
{
    return skippedRooms_;
}

std::string ApplyDetectionCommand::name() const
{
    return "Accept detection";
}

// Adds the new walls and rooms one by one on the first call, and replays the same steps for redo.
Status ApplyDetectionCommand::apply(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");

    if (!steps_.empty()) {
        for (std::size_t i = 0; i < steps_.size(); ++i) {
            const Status s = steps_[i]->apply(project);
            if (!s.ok()) {
                for (std::size_t j = i; j-- > 0;) steps_[j]->undo(project);
                return s;
            }
        }
        return {};
    }

    if (!proposals_.status.ok()) return fail(ErrorCode::InvalidValue, "The detection did not succeed: " + proposals_.status.message);
    if (proposals_.cancelled) return fail(ErrorCode::InvalidValue, "The detection was stopped before it finished.");
    if (const Status s = checkDetectionResult(proposals_); !s.ok()) return s;

    DetectionResult marked = proposals_;
    markExisting(*floor, marked);

    std::vector<std::unique_ptr<Command>> steps;
    std::vector<Id> wallIds;
    std::vector<Id> pointIds;
    std::vector<Id> splitIds;
    std::vector<Id> roomIds;
    std::size_t skippedWalls = 0;
    std::size_t skippedRooms = 0;
    std::vector<std::optional<Id>> planPoint(marked.points.size());

    const auto endFor = [&](std::size_t index, double thickness) {
        if (planPoint[index]) return WallEnd::atPoint(*planPoint[index]);
        SnapOptions options;
        options.radius = std::max(thickness / 2.0, kMinSnapRadius);
        return wallEndFromSnap(snapPosition(*floor, marked.points[index], options));
    };

    for (const ProposedWall& w : marked.walls) {
        if (w.state != ProposalState::New) {
            ++skippedWalls;
            continue;
        }
        auto draw = std::make_unique<DrawWallCommand>(floorId_, endFor(w.from, w.thickness), endFor(w.to, w.thickness), w.thickness);
        if (!draw->apply(project).ok()) {
            ++skippedWalls;
            continue;
        }
        planPoint[w.from] = draw->fromPointId();
        planPoint[w.to] = draw->toPointId();
        wallIds.push_back(draw->wallId());
        for (const Id& id : draw->newPointIds()) pointIds.push_back(id);
        for (const Id& id : draw->splitWallIds()) addOnce(splitIds, id);
        steps.push_back(std::move(draw));
    }

    if (addRooms_) {
        for (const ProposedRoom& room : marked.rooms) {
            auto add = std::make_unique<AddRoomCommand>(floorId_, room.inside);
            if (room.state != ProposalState::New || !add->apply(project).ok()) {
                ++skippedRooms;
                continue;
            }
            roomIds.push_back(add->roomId());
            steps.push_back(std::move(add));
        }
    }

    if (steps.empty()) return fail(ErrorCode::AlreadyExists, "There is nothing new to add. The floor already has these walls and rooms.");

    std::erase_if(splitIds, [&](const Id& id) { return std::find(wallIds.begin(), wallIds.end(), id) != wallIds.end(); });
    steps_ = std::move(steps);
    wallIds_ = std::move(wallIds);
    pointIds_ = std::move(pointIds);
    splitWallIds_ = std::move(splitIds);
    roomIds_ = std::move(roomIds);
    skippedWalls_ = skippedWalls;
    skippedRooms_ = skippedRooms;
    return {};
}

// Undoes the steps in reverse order.
void ApplyDetectionCommand::undo(Project& project)
{
    for (std::size_t i = steps_.size(); i-- > 0;) steps_[i]->undo(project);
}

unsigned ApplyDetectionCommand::changes() const
{
    return Change::Points | Change::Walls | Change::Rooms | Change::Openings;
}

Id ApplyDetectionCommand::floorId() const
{
    return floorId_;
}

}
