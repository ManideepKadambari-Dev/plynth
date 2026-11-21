#pragma once

#include "../Command.h"

#include <optional>
#include <string>

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

// Sets or clears the scale of the plan. InvalidValue unless the value is finite and above zero.
// Points keep their metre coordinates; whether setting the scale should also move them is decided in milestone 3.
class SetScaleCommand : public Command {
public:
    explicit SetScaleCommand(std::optional<double> pixelsPerMetre);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;

private:
    std::optional<double> pixelsPerMetre_;
    std::optional<double> previous_;
};

}
