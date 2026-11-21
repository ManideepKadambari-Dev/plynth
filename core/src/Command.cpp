#include "plynth/core/Command.h"

#include <utility>

namespace plynth {

CommandGroup::CommandGroup(std::string name) : name_(std::move(name)) {}

// Adds a command to the end of the group.
void CommandGroup::add(std::unique_ptr<Command> command)
{
    commands_.push_back(std::move(command));
}

// Says whether the group has no commands.
bool CommandGroup::empty() const
{
    return commands_.empty();
}

std::string CommandGroup::name() const
{
    return name_;
}

// Applies the commands in order, and undoes the ones before a failing command.
Status CommandGroup::apply(Project& project)
{
    for (std::size_t i = 0; i < commands_.size(); ++i) {
        Status s = commands_[i]->apply(project);
        if (!s.ok()) {
            for (std::size_t k = i; k > 0; --k) commands_[k - 1]->undo(project);
            return s;
        }
    }
    return {};
}

// Undoes the commands in reverse order.
void CommandGroup::undo(Project& project)
{
    for (std::size_t k = commands_.size(); k > 0; --k) commands_[k - 1]->undo(project);
}

// All change flags of the commands in the group.
unsigned CommandGroup::changes() const
{
    unsigned flags = 0;
    for (const auto& c : commands_) flags |= c->changes();
    return flags;
}

// The floor shared by all commands, or empty when they differ.
Id CommandGroup::floorId() const
{
    Id shared;
    for (const auto& c : commands_) {
        const Id id = c->floorId();
        if (id.empty()) return {};
        if (shared.empty()) shared = id;
        else if (shared != id) return {};
    }
    return shared;
}

}
