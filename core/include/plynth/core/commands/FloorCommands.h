#pragma once

#include "../Command.h"

#include <optional>
#include <string>
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

// What stays put when the scale changes.
// ImagePositions: marks stay on the same spot of the plan image, so their metre values change.
// Metres: metre values stay, so marks move on the image.
enum class ScaleKeep { ImagePositions, Metres };

// Sets or clears the scale of the plan. InvalidValue unless the value is finite and above zero.
// When the plan already had a scale, the new one differs and keep is ImagePositions, every point position,
// wall bulge and opening offset on every floor is multiplied by old / new. Thickness, heights, widths, sills
// and corner radius are real sizes and never change. Setting the first scale or clearing it moves nothing.
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
    // Scale, plus Points, Walls and Openings when apply moved marks.
    unsigned changes() const override;

private:
    std::optional<double> pixelsPerMetre_;
    ScaleKeep keep_ = ScaleKeep::ImagePositions;
    std::optional<double> previous_;
    bool moved_ = false;
    std::vector<Floor> previousFloors_; // the floors before a move, for an exact undo
};

}
