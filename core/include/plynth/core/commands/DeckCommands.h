#pragma once

#include "../Command.h"

#include <cstddef>
#include <string>
#include <vector>

namespace plynth {

// Decks (Decks.h). Outside steps use the stair commands with Stair::rise set.

// Adds a deck to a floor with the values of deck (its id is ignored; the new one is newId with kDeckIdPrefix,
// the same on redo). NotFound for an unknown floor, and whatever checkDeck refuses.
class AddDeckCommand : public Command {
public:
    AddDeckCommand(Id floorId, Deck deck);

    // The id the deck got; empty before the first apply.
    const Id& newDeckId() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Outdoor.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id floorId_;
    Deck deck_;
};

// Replaces every value of a deck except its id. Railings hosted on it follow its new top. Changes with the same
// non zero mergeKey and deck merge into one undo step, for a drag of the outline. NotFound, and checkDeck.
class SetDeckCommand : public Command {
public:
    SetDeckCommand(Id deckId, Deck values, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Outdoor.
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id deckId_;
    Deck values_;
    int mergeKey_;
    Id floorId_;
    Deck previous_;
};

// Removes a deck. Railings hosted on it keep their line and stand on base. NotFound for an unknown deck.
class RemoveDeckCommand : public Command {
public:
    explicit RemoveDeckCommand(Id deckId);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Outdoor.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id deckId_;
    Id floorId_;
    Deck removed_;
    std::size_t index_ = 0;
    std::vector<Id> hosted_; // the railings that stood on it
};

}
