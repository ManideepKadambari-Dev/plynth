#include "plynth/core/commands/SettingsCommands.h"

#include "CommandSupport.h"

namespace plynth {

SetGridSizeCommand::SetGridSizeCommand(double gridSize) : gridSize_(gridSize) {}

std::string SetGridSizeCommand::name() const
{
    return "Set grid size";
}

// Sets the grid size, remembering the old one.
Status SetGridSizeCommand::apply(Project& project)
{
    if (!detail::isPositive(gridSize_)) {
        return fail(ErrorCode::InvalidValue, "The grid size must be a number above zero.");
    }
    previous_ = project.settings.gridSize;
    project.settings.gridSize = gridSize_;
    return {};
}

// Puts the old grid size back.
void SetGridSizeCommand::undo(Project& project)
{
    project.settings.gridSize = previous_;
}

unsigned SetGridSizeCommand::changes() const
{
    return Change::Settings;
}

}

namespace plynth {

SetDefaultSizesCommand::SetDefaultSizesCommand(DefaultSizes sizes) : sizes_(sizes) {}

std::string SetDefaultSizesCommand::name() const
{
    return "Set default sizes";
}

// Sets the default sizes, remembering the old ones.
Status SetDefaultSizesCommand::apply(Project& project)
{
    if (Status s = checkDefaultSizes(sizes_); !s.ok()) return s;
    previous_ = project.settings.defaults;
    project.settings.defaults = sizes_;
    return {};
}

// Puts the old default sizes back.
void SetDefaultSizesCommand::undo(Project& project)
{
    project.settings.defaults = previous_;
}

unsigned SetDefaultSizesCommand::changes() const
{
    return Change::Settings;
}

}
