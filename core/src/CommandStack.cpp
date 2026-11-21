#include "plynth/core/CommandStack.h"

#include <algorithm>
#include <utility>

namespace plynth {

CommandStack::CommandStack(std::size_t limit) : limit_(std::max<std::size_t>(limit, 1)) {}

// Applies a command and records it, merging it into the previous one when allowed.
Status CommandStack::push(std::unique_ptr<Command> command, Project& project, bool* merged)
{
    if (merged) *merged = false;
    if (Status s = command->apply(project); !s.ok()) return s;

    commands_.resize(position_);
    if (saved_ && *saved_ > position_) saved_.reset();

    const bool atSaved = saved_ && *saved_ == position_;
    if (position_ > 0 && !atSaved && commands_[position_ - 1]->mergeWith(*command)) {
        if (merged) *merged = true;
        return {};
    }

    commands_.push_back(std::move(command));
    ++position_;
    if (commands_.size() > limit_) {
        commands_.erase(commands_.begin());
        --position_;
        if (saved_) {
            if (*saved_ == 0) saved_.reset();
            else --*saved_;
        }
    }
    return {};
}

bool CommandStack::canUndo() const
{
    return position_ > 0;
}

bool CommandStack::canRedo() const
{
    return position_ < commands_.size();
}

// The command undo would run next.
const Command* CommandStack::nextUndo() const
{
    return canUndo() ? commands_[position_ - 1].get() : nullptr;
}

// The command redo would run next.
const Command* CommandStack::nextRedo() const
{
    return canRedo() ? commands_[position_].get() : nullptr;
}

// Undoes the last applied command.
void CommandStack::undo(Project& project)
{
    if (!canUndo()) return;
    commands_[position_ - 1]->undo(project);
    --position_;
}

// Applies the next undone command again, and drops the redo list when it fails.
Status CommandStack::redo(Project& project)
{
    if (!canRedo()) return {};
    Status s = commands_[position_]->apply(project);
    if (!s.ok()) {
        commands_.resize(position_);
        if (saved_ && *saved_ > position_) saved_.reset();
        return s;
    }
    ++position_;
    return {};
}

// Forgets all commands and takes the current state as the saved one.
void CommandStack::clear()
{
    commands_.clear();
    position_ = 0;
    saved_ = 0;
}

// Says whether the project is in the state of the last save.
bool CommandStack::isAtSaved() const
{
    return saved_ && *saved_ == position_;
}

// Records the current state as the saved one.
void CommandStack::markSaved()
{
    saved_ = position_;
}

// Records that no state in the history matches the files.
void CommandStack::markNeverSaved()
{
    saved_.reset();
}

std::size_t CommandStack::count() const
{
    return commands_.size();
}

std::size_t CommandStack::position() const
{
    return position_;
}

}
