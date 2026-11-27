#pragma once

#include "Plan.h"
#include "Project.h"
#include "Status.h"

#include <memory>
#include <string>
#include <vector>

namespace plynth {

// Flags saying which parts of a document changed. Combine with |.
namespace Change {
inline constexpr unsigned Settings = 1u << 0;
inline constexpr unsigned Scale = 1u << 1;
inline constexpr unsigned Floors = 1u << 2;     // floors added, removed, renamed, reordered, a ceiling height or slab changed
inline constexpr unsigned FloorImage = 1u << 3; // a floor's image or image offset changed
inline constexpr unsigned Points = 1u << 4;
inline constexpr unsigned Walls = 1u << 5;
inline constexpr unsigned Rooms = 1u << 6;
inline constexpr unsigned Openings = 1u << 7;
inline constexpr unsigned Items = 1u << 8;
inline constexpr unsigned History = 1u << 9;       // undo, redo or the unsaved changes state
inline constexpr unsigned Folder = 1u << 10;       // the document now belongs to another folder
inline constexpr unsigned ReadOnly = 1u << 11;     // the document became read only or editable again
inline constexpr unsigned Stairs = 1u << 12;
inline constexpr unsigned Roofs = 1u << 13;        // the roof default or a roof section entry changed
inline constexpr unsigned Lights = 1u << 14;       // a light marker or light strip was added, moved, changed or removed
// Decks, railings, canopies, the site and the foundation (plan format 8). Cladding zones count as Walls, dormers
// as Roofs, the stacking of items as Items.
inline constexpr unsigned Outdoor = 1u << 15;
// Every part of the project above. Roof sections are worked out from walls and rooms, so views that show them
// also redraw on Floors, Walls and Rooms.
inline constexpr unsigned Content = ((1u << 9) - 1) | Stairs | Roofs | Lights | Outdoor;
inline constexpr unsigned Everything = Content | History | Folder | ReadOnly;
}

// One change to a project that can be undone. Every edit made by the app or the CLI is a command.
// Commands never touch the disk.
class Command {
public:
    virtual ~Command() = default;

    // Short name shown after "Undo" and "Redo" in the Edit menu, for example "Add wall".
    virtual std::string name() const = 0;

    // Makes the change. Checks everything first: when it returns an error the project is unchanged.
    // Called again for redo, on the same state the first call saw, so it must give the same result
    // (for example the same new id). After it succeeds, checkPlan on the plan still passes.
    virtual Status apply(Project& project) = 0;

    // Takes the change back. Only called right after apply succeeded, on the state apply left.
    virtual void undo(Project& project) = 0;

    // Which parts of the project this command changes, as Change flags.
    virtual unsigned changes() const = 0;

    // The floor the change is on, or empty when it is not on one floor.
    virtual Id floorId() const { return {}; }

    // Tries to fold the next command, already applied, into this one so both undo as one step,
    // for example the many small moves of one drag. Returns true when it did; next is then discarded.
    virtual bool mergeWith(const Command& next)
    {
        (void)next;
        return false;
    }
};

// Several commands that apply and undo as one step, for example deleting a selection.
class CommandGroup : public Command {
public:
    explicit CommandGroup(std::string name);

    // Adds a command to the end of the group. Only before the group is applied.
    void add(std::unique_ptr<Command> command);
    bool empty() const;

    std::string name() const override;
    // Applies the commands in order. If one fails, undoes the ones before it and returns its error.
    Status apply(Project& project) override;
    // Undoes the commands in reverse order.
    void undo(Project& project) override;
    // All flags of the commands in the group.
    unsigned changes() const override;
    // The floor shared by all commands, or empty when they differ.
    Id floorId() const override;

private:
    std::string name_;
    std::vector<std::unique_ptr<Command>> commands_;
};

}
