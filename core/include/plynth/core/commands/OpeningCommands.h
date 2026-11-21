#pragma once

#include "../Command.h"

#include <cstddef>
#include <string>

namespace plynth {

// Every opening command checks the result with checkOpeningFit (Openings.h) before changing anything, and
// refuses with what it says. Ids come from newId with kOpeningIdPrefix.

// Adds an opening on the wall named by opening.wall, at the end of the floor's opening list. opening.id is
// ignored: the new id comes from newId on the first apply and is kept for redo. NotFound for an unknown
// wall, otherwise as checkOpeningFit.
class AddOpeningCommand : public Command {
public:
    explicit AddOpeningCommand(Opening opening);

    // The id of the new opening. Empty until apply has succeeded once.
    const Id& openingId() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Openings.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id floorId_;
    Opening opening_;
};

// Moves an opening along a wall and up or down it: a new wall (on the same floor), offset and sill. Size,
// shape, type, hinge and swing stay. NotFound for an unknown opening or wall, InvalidValue for a wall on
// another floor, otherwise as checkOpeningFit. Moves with the same non zero mergeKey and opening merge into
// one undo step, so the app gives each drag its own key.
class MoveOpeningCommand : public Command {
public:
    MoveOpeningCommand(Id openingId, Id wallId, double offset, double sill, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id openingId_;
    Id wallId_;
    double offset_;
    double sill_;
    int mergeKey_;
    Id floorId_;
    Opening previous_;
};

// Gives an opening a new box: offset and width along the wall, sill and height up it. The whole box is given
// so dragging one edge in the wall view can keep the opposite edge still. NotFound for an unknown opening,
// otherwise as checkOpeningFit. Merges like MoveOpeningCommand.
class ResizeOpeningCommand : public Command {
public:
    ResizeOpeningCommand(Id openingId, double offset, double width, double sill, double height, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id openingId_;
    double offset_;
    double width_;
    double sill_;
    double height_;
    int mergeKey_;
    Id floorId_;
    Opening previous_;
};

// What an opening is, apart from where it is and how big.
struct OpeningStyle {
    OpeningType type = OpeningType::Door;
    std::string shape = "rectangle";
    HingeEnd hinge = HingeEnd::Start;
    WallSide swing = WallSide::Right;

    bool operator==(const OpeningStyle&) const = default;
};

// Sets the type, shape, hinge and swing of an opening. Sizes stay as they are, also when the type changes.
// NotFound for an unknown opening, otherwise as checkOpeningFit (for example a round arch on an opening lower
// than half its width).
class SetOpeningStyleCommand : public Command {
public:
    SetOpeningStyleCommand(Id openingId, OpeningStyle style);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id openingId_;
    OpeningStyle style_;
    Id floorId_;
    OpeningStyle previous_;
};

// Removes an opening. NotFound for an unknown opening. Undo puts it back at the same place in the list.
class RemoveOpeningCommand : public Command {
public:
    explicit RemoveOpeningCommand(Id openingId);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id openingId_;
    Id floorId_;
    Opening removed_;
    std::size_t index_ = 0;
};

}
