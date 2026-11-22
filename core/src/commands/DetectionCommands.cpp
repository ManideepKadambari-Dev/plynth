#include "plynth/core/commands/DetectionCommands.h"

#include "plynth/core/Geometry.h"
#include "plynth/core/Openings.h"
#include "plynth/core/Snapping.h"
#include "plynth/core/WallGeometry.h"
#include "plynth/core/commands/OpeningCommands.h"
#include "plynth/core/commands/RoomCommands.h"
#include "plynth/core/commands/WallCommands.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

namespace plynth {

namespace {

// Proposals are in metres from the image's top left corner; the floor's image may sit elsewhere, so every
// position goes through imageToPlan.
void shiftByImageOffset(const Floor& floor, DetectionResult& result)
{
    if (floor.imageOffset == ImageOffset{}) return;
    const double ppm = result.pixelsPerMetre > 0.0 ? result.pixelsPerMetre : 1.0;
    const auto move = [&](Vec2& p) {
        const Vec2 q = imageToPlan(floor, ppm, {p.x * ppm, p.y * ppm});
        p = q;
    };
    for (Vec2& p : result.points) move(p);
    for (ProposedRoom& room : result.rooms) {
        move(room.inside);
        for (std::vector<Vec2>& ring : room.rings) {
            for (Vec2& p : ring) move(p);
        }
    }
}

constexpr double kMinSnapRadius = 0.05; // metres

constexpr double kSameWayDegrees = 5.0; // how far two walls may differ in direction to count as the same wall

// The plan wall that holds the centre of a proposed opening: the nearest straight wall whose centre line is within
// the tolerance of the centre and runs the same way as the proposal's wall. Null when there is none.
const Wall* wallForOpening(const Floor& floor, Vec2 centre, Vec2 direction, double proposedThickness)
{
    const double maxSin = std::sin(kSameWayDegrees * std::numbers::pi / 180.0);
    const Wall* best = nullptr;
    double bestDistance = 0.0;
    for (const Wall& w : floor.walls) {
        if (std::fabs(w.bulge) >= kMinBulge) continue;
        const WallCurve curve = wallCurve(floor, w);
        if (!curve.valid) continue;
        const Vec2 d = tangentAlong(curve, 0.0);
        if (std::fabs(direction.x * d.y - direction.y * d.x) > maxSin) continue;
        const double tolerance = std::max(0.5 * std::max(proposedThickness, w.thickness), kMinSnapRadius);
        const double gap = projectOntoCurve(centre, curve).distance;
        if (gap > tolerance) continue;
        if (!best || gap < bestDistance) {
            best = &w;
            bestDistance = gap;
        }
    }
    return best;
}

// Moves an opening into the free part of its wall when it overhangs by no more than the slack, and narrows it when
// it is wider than the free part. Returns false when it overhangs by more than the slack.
bool fitIntoFreePart(const Floor& floor, const Wall& wall, Opening& opening)
{
    const WallSpan span = wallSpan(floor, wall);
    const double left = opening.offset - opening.width / 2.0;
    const double right = opening.offset + opening.width / 2.0;
    const double overhang = std::max({span.start - left, right - span.end, 0.0});
    if (overhang <= 0.0) return true;
    if (overhang > kDetectedOpeningSlack + 1e-9) return false;
    const double free = span.end - span.start;
    if (opening.width > free) {
        if (!(free > 0.0)) return false;
        opening.width = free;
        opening.offset = (span.start + span.end) / 2.0;
    } else {
        opening.offset = std::clamp(opening.offset, span.start + opening.width / 2.0, span.end - opening.width / 2.0);
    }
    return true;
}

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

const std::vector<Id>& ApplyDetectionCommand::openingIds() const
{
    return openingIds_;
}

const std::vector<Id>& ApplyDetectionCommand::roomIds() const
{
    return roomIds_;
}

std::size_t ApplyDetectionCommand::skippedWalls() const
{
    return skippedWalls_;
}

std::size_t ApplyDetectionCommand::skippedOpenings() const
{
    return skippedOpenings_;
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
    shiftByImageOffset(*floor, marked);
    markExisting(*floor, marked);

    std::vector<std::unique_ptr<Command>> steps;
    std::vector<Id> wallIds;
    std::vector<Id> pointIds;
    std::vector<Id> splitIds;
    std::vector<Id> roomIds;
    std::vector<Id> openingIds;
    std::size_t skippedWalls = 0;
    std::size_t skippedOpenings = 0;
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

    for (const ProposedOpening& proposed : marked.openings) {
        if (proposed.state != ProposalState::New || proposed.wall >= marked.walls.size()) {
            ++skippedOpenings;
            continue;
        }
        const ProposedWall& pw = marked.walls[proposed.wall];
        const Vec2 a = marked.points[pw.from];
        const Vec2 b = marked.points[pw.to];
        const double length = distance(a, b);
        if (length <= 0.0) {
            ++skippedOpenings;
            continue;
        }
        const Vec2 direction{(b.x - a.x) / length, (b.y - a.y) / length};
        const Vec2 centre{a.x + direction.x * proposed.offset, a.y + direction.y * proposed.offset};
        const Wall* wall = wallForOpening(*floor, centre, direction, pw.thickness);
        if (!wall) {
            ++skippedOpenings;
            continue;
        }
        const WallCurve curve = wallCurve(*floor, *wall);
        const Vec2 wallDirection = tangentAlong(curve, 0.0);
        const bool sameWay = direction.x * wallDirection.x + direction.y * wallDirection.y > 0.0;

        Opening opening = defaultOpening(project.settings.defaults, proposed.type);
        opening.wall = wall->id;
        opening.offset = projectOntoCurve(centre, curve).offset;
        opening.width = proposed.width;
        opening.hinge = sameWay ? proposed.hinge : otherEnd(proposed.hinge);
        opening.swing = sameWay ? proposed.swing : otherSide(proposed.swing);
        if (!fitIntoFreePart(*floor, *wall, opening)) {
            ++skippedOpenings;
            continue;
        }
        auto add = std::make_unique<AddOpeningCommand>(opening);
        if (!add->apply(project).ok()) {
            ++skippedOpenings;
            continue;
        }
        openingIds.push_back(add->openingId());
        steps.push_back(std::move(add));
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

    if (steps.empty()) {
        return fail(ErrorCode::AlreadyExists, "There is nothing new to add. The floor already has these walls, openings and rooms.");
    }

    std::erase_if(splitIds, [&](const Id& id) { return std::find(wallIds.begin(), wallIds.end(), id) != wallIds.end(); });
    steps_ = std::move(steps);
    wallIds_ = std::move(wallIds);
    pointIds_ = std::move(pointIds);
    splitWallIds_ = std::move(splitIds);
    roomIds_ = std::move(roomIds);
    openingIds_ = std::move(openingIds);
    skippedOpenings_ = skippedOpenings;
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
    return Change::Points | Change::Walls | Change::Rooms | Change::Openings | Change::Items;
}

Id ApplyDetectionCommand::floorId() const
{
    return floorId_;
}

}
