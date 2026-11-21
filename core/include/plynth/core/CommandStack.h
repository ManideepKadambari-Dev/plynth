#pragma once

#include "Command.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace plynth {

inline constexpr std::size_t kDefaultUndoLimit = 1000;

// The undo history: commands that were applied, in order, and a position in that list.
// Commands before the position can be undone, commands after it can be redone.
class CommandStack {
public:
    explicit CommandStack(std::size_t limit = kDefaultUndoLimit);

    // Applies a command to the project and records it. On error nothing is recorded and the project is
    // unchanged. On success the redo list is dropped, then the command is merged into the one before it
    // when that one accepts it (never across the saved point), otherwise it is added. The oldest commands
    // are dropped past the limit. merged, when given, tells whether a merge happened.
    Status push(std::unique_ptr<Command> command, Project& project, bool* merged = nullptr);

    bool canUndo() const;
    bool canRedo() const;

    // The command undo or redo would run next, or null when there is none.
    const Command* nextUndo() const;
    const Command* nextRedo() const;

    // Undoes the last applied command. Does nothing when there is none.
    void undo(Project& project);

    // Applies the next undone command again. If it fails the redo list is dropped and its error returned.
    Status redo(Project& project);

    // Forgets all commands. The current state becomes the saved state.
    void clear();

    // Says whether the project is in the state it had when markSaved was last called.
    bool isAtSaved() const;

    // Records the current state as the saved one.
    void markSaved();

    // Records that no state in the history matches the files on disk, for example after recovering an autosave.
    void markNeverSaved();

    std::size_t count() const;
    std::size_t position() const;

private:
    std::vector<std::unique_ptr<Command>> commands_;
    std::size_t position_ = 0;
    std::optional<std::size_t> saved_ = 0;
    std::size_t limit_;
};

}
