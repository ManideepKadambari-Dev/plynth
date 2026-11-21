#pragma once

#include "../Command.h"
#include "../detection/Detection.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace plynth {

// Accepts detection proposals: turns them into ordinary points, walls, openings and rooms of a floor, as one
// undo step named "Accept detection".
// On the first apply it runs markExisting on its own copy of the proposals against the floor as it is then,
// and builds its steps:
// - Each New wall, in proposal order, becomes a DrawWallCommand. Each end is found when that wall is drawn:
//   a proposal point already turned into a plan point by an earlier wall is reused; otherwise the end is
//   snapped with snapPosition (points and walls only, no grid, radius half the wall's thickness and at least
//   0.05 m) and turned into a WallEnd with wallEndFromSnap, so it lands on an existing point, splits an
//   existing wall (a T junction), or adds a new point. A wall the DrawWallCommand refuses is skipped.
// - Each New opening is then added with AddOpeningCommand on the plan wall that now holds
//   its centre: the nearest straight wall of the floor whose centre line is within the tolerance of
//   markExisting and runs the same way as the proposal's wall (within 5 degrees). Its offset is measured on
//   that wall; when that wall runs the other way, hinge and swing are turned round (otherEnd, otherSide).
//   Width comes from the proposal, height and sill from the project's default sizes for its type
//   (defaultOpening), shape rectangle. An opening with no such wall, or that AddOpeningCommand refuses (for
//   example it overlaps an opening already there), is skipped. There are no opening proposals when detection
//   ran with params.openings false, so that setting also decides whether Accept adds openings.
// - With addRooms, each New room is then added with AddRoomCommand at its inside position, with no type.
//   A room it refuses (no closed area there, or already a room) is skipped.
// Redo replays the same steps, so the ids are the same. Undo takes all of it back.
// NotFound for an unknown floor. InvalidValue when the proposals have an error status, were cancelled or
// break checkDetectionResult. AlreadyExists when nothing would be added (every wall, opening and room skipped).
class ApplyDetectionCommand : public Command {
public:
    ApplyDetectionCommand(Id floorId, DetectionResult proposals, bool addRooms = true);

    // What the first apply did. Empty before it has succeeded once.
    const std::vector<Id>& wallIds() const;      // walls added, in proposal order
    const std::vector<Id>& pointIds() const;     // points added, in the order they were made
    const std::vector<Id>& splitWallIds() const; // existing walls split to make T junctions
    const std::vector<Id>& openingIds() const;   // openings added, in proposal order
    const std::vector<Id>& roomIds() const;      // rooms added
    std::size_t skippedWalls() const;           // Existing, Overlaps or refused
    std::size_t skippedOpenings() const;        // Existing, no wall found or refused
    std::size_t skippedRooms() const;           // Existing or refused

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Points, Walls, Rooms and Openings.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id floorId_;
    DetectionResult proposals_;
    bool addRooms_;
    std::vector<Id> wallIds_;
    std::vector<Id> pointIds_;
    std::vector<Id> splitWallIds_;
    std::vector<Id> openingIds_;
    std::vector<Id> roomIds_;
    std::size_t skippedWalls_ = 0;
    std::size_t skippedOpenings_ = 0;
    std::size_t skippedRooms_ = 0;
    std::vector<std::unique_ptr<Command>> steps_; // made on the first apply, applied again for redo
};

}
