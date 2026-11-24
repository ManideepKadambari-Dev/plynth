#pragma once

#include "../Command.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace plynth {

// Sets or clears the plan image of a floor. The path is stored as given (see Floor::image);
// the caller checks the file with probePlanImage first. NotFound for an unknown floor,
// InvalidValue for an empty path.
class SetFloorImageCommand : public Command {
public:
    SetFloorImageCommand(Id floorId, std::optional<std::string> image);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id floorId_;
    std::optional<std::string> image_;
    std::optional<std::string> previous_;
};

// Sets the ceiling height of a floor, which is also the height of every wall without its own height.
// NotFound for an unknown floor. InvalidValue when the height is not finite and above zero, or when an opening
// on a wall without its own height would then reach above it (the message names it). Changes with the same
// non zero mergeKey and floor merge into one undo step.
class SetCeilingHeightCommand : public Command {
public:
    SetCeilingHeightCommand(Id floorId, double height, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Floors.
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id floorId_;
    double height_;
    int mergeKey_;
    double previous_ = 0.0;
};

// Adds an empty floor named name (InvalidValue when empty after trimming spaces) with the project's default
// ceiling height, kDefaultSlabThickness and no image, at position in plan order (0 is below every floor; empty
// or the floor count puts it on top). InvalidValue for a position past the end. The id is newId with
// kFloorIdPrefix, the same on redo, which refuses with AlreadyExists when another floor or object has that id by
// then.
class AddFloorCommand : public Command {
public:
    AddFloorCommand(std::string name, std::optional<std::size_t> position = {});

    // The id the floor got; empty before the first apply.
    const Id& newFloorId() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Floors.
    unsigned changes() const override;
    Id floorId() const override;

private:
    std::string name_;
    std::optional<std::size_t> position_;
    Id id_;
};

// Removes a floor with everything on it, and the roof section entries tied to it. InvalidValue for the only
// floor, NotFound for an unknown one. Stairs on the floor below then have no floor above or reach the next one
// up; stairProblems reports it.
class RemoveFloorCommand : public Command {
public:
    explicit RemoveFloorCommand(Id floorId);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Floors, Points, Walls, Rooms, Openings, Items, Stairs, Roofs.
    unsigned changes() const override;

private:
    Id floorId_;
    std::optional<Floor> removed_;
    std::size_t index_ = 0;
    std::vector<std::pair<std::size_t, RoofSection>> removedSections_;
};

// Renames a floor. InvalidValue for a name that is empty after trimming spaces, NotFound for an unknown floor.
class SetFloorNameCommand : public Command {
public:
    SetFloorNameCommand(Id floorId, std::string name);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Floors.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id floorId_;
    std::string name_;
    std::string previous_;
};

// Sets the slab thickness of a floor. InvalidValue outside kMinSlabThickness to kMaxSlabThickness, NotFound
// for an unknown floor. Changes with the same non zero mergeKey and floor merge into one undo step.
class SetSlabThicknessCommand : public Command {
public:
    SetSlabThicknessCommand(Id floorId, double thickness, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Floors.
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id floorId_;
    double thickness_;
    int mergeKey_;
    double previous_ = 0.0;
};

// Moves a floor's plan image to a new offset (ImageOffset in Plan.h), to line it up with the floor below.
// With moveMarks every point, item, stair and roof section anchor of that floor moves by the same amount, so
// marks traced on the image stay on it; otherwise only the image moves. InvalidValue for a value that is not
// finite, NotFound for an unknown floor. Changes with the same non zero mergeKey and floor merge, for a drag.
class SetFloorImageOffsetCommand : public Command {
public:
    SetFloorImageOffsetCommand(Id floorId, ImageOffset offset, bool moveMarks = true, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // FloorImage, plus Points, Walls, Openings, Items, Stairs and Roofs when marks moved.
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id floorId_;
    ImageOffset offset_;
    bool moveMarks_;
    int mergeKey_;
    Floor previousFloor_;                      // the floor before the move, for an exact undo
    std::vector<RoofSection> previousSections_;
};

// What stays put when the scale changes.
// ImagePositions: marks stay on the same spot of the plan image, so their metre values change.
// Metres: metre values stay, so marks move on the image.
enum class ScaleKeep { ImagePositions, Metres };

// Sets or clears the scale of the plan. InvalidValue unless the value is finite and above zero.
// When the plan already had a scale, the new one differs and keep is ImagePositions, every point position,
// wall bulge and opening offset on every floor is multiplied by old / new, and so is every item,
// stair and roof section anchor position. Positions scale about their floor's image offset: p becomes
// offset + (p - offset) * old / new.
// Thickness, heights, widths, sills and corner radius are real sizes and never change. Setting the first scale
// or clearing it moves nothing.
class SetScaleCommand : public Command {
public:
    // Keeps image positions.
    explicit SetScaleCommand(std::optional<double> pixelsPerMetre);
    SetScaleCommand(std::optional<double> pixelsPerMetre, ScaleKeep keep);

    // True when the last apply moved marks.
    bool movedMarks() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Scale, plus Points, Walls, Openings, Items, Stairs and Roofs when apply moved marks.
    unsigned changes() const override;

private:
    std::optional<double> pixelsPerMetre_;
    ScaleKeep keep_ = ScaleKeep::ImagePositions;
    std::optional<double> previous_;
    bool moved_ = false;
    std::vector<Floor> previousFloors_; // the floors before a move, for an exact undo
    std::vector<RoofSection> previousRoofSections_;
};

}
