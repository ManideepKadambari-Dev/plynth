#pragma once

#include "../Command.h"

#include <string>

namespace plynth {

// Sets the project's snap grid size in metres (DESIGN.md section 7.2). InvalidValue unless finite and
// above zero. The grid is per project; whether it should be per floor is open decision 2.
class SetGridSizeCommand : public Command {
public:
    explicit SetGridSizeCommand(double gridSize);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Settings.
    unsigned changes() const override;

private:
    double gridSize_;
    double previous_ = 0.0;
};

// Sets the project's default sizes for new openings, new floors and newly mounted wall items (DESIGN.md
// section 7.5). Marks already placed keep their sizes and heights. InvalidValue from checkDefaultSizes.
class SetDefaultSizesCommand : public Command {
public:
    explicit SetDefaultSizesCommand(DefaultSizes sizes);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Settings.
    unsigned changes() const override;

private:
    DefaultSizes sizes_;
    DefaultSizes previous_;
};

}
