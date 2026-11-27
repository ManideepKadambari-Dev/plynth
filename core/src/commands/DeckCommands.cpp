#include "plynth/core/commands/DeckCommands.h"

#include "plynth/core/Decks.h"

#include <algorithm>
#include <utility>

namespace plynth {

namespace {

// A deck and the floor it is on.
struct Located {
    Floor* floor = nullptr;
    Deck* deck = nullptr;
};

// Finds a deck anywhere in the plan.
Located locate(Project& project, const Id& deckId)
{
    Located found;
    for (Floor& floor : project.plan.floors) {
        if (Deck* deck = findDeck(floor, deckId)) {
            found.floor = &floor;
            found.deck = deck;
            break;
        }
    }
    return found;
}

// The refusal for a deck that is not there.
Status noDeck(const Id& deckId)
{
    return fail(ErrorCode::NotFound, "There is no deck " + deckId + ".");
}

}

AddDeckCommand::AddDeckCommand(Id floorId, Deck deck) : floorId_(std::move(floorId)), deck_(std::move(deck))
{
    deck_.id.clear();
}

// The id of the new deck, empty until the first apply.
const Id& AddDeckCommand::newDeckId() const
{
    return deck_.id;
}

std::string AddDeckCommand::name() const
{
    return "Add deck";
}

// Checks the deck and adds it at the end of the floor's list.
Status AddDeckCommand::apply(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");
    Deck deck = deck_;
    if (const Status s = checkDeck(deck); !s.ok()) return s;
    if (deck.id.empty()) {
        deck.id = newId(project.plan, kDeckIdPrefix);
    } else if (idInUse(project.plan, deck.id)) {
        return fail(ErrorCode::AlreadyExists, "The id " + deck.id + " is already used.");
    }
    deck_.id = deck.id;
    floor->decks.push_back(std::move(deck));
    return {};
}

// Removes the deck that apply added.
void AddDeckCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    std::erase_if(floor->decks, [&](const Deck& d) { return d.id == deck_.id; });
}

unsigned AddDeckCommand::changes() const
{
    return Change::Outdoor;
}

Id AddDeckCommand::floorId() const
{
    return floorId_;
}

SetDeckCommand::SetDeckCommand(Id deckId, Deck values, int mergeKey)
    : deckId_(std::move(deckId)), values_(std::move(values)), mergeKey_(mergeKey)
{
    values_.id = deckId_;
}

std::string SetDeckCommand::name() const
{
    return "Change deck";
}

// Checks the values and replaces everything but the id, keeping the old deck for undo.
Status SetDeckCommand::apply(Project& project)
{
    const Located found = locate(project, deckId_);
    if (!found.deck) return noDeck(deckId_);
    if (const Status s = checkDeck(values_); !s.ok()) return s;
    floorId_ = found.floor->id;
    previous_ = *found.deck;
    *found.deck = values_;
    return {};
}

// Puts the deck back as it was.
void SetDeckCommand::undo(Project& project)
{
    const Located found = locate(project, deckId_);
    if (found.deck) *found.deck = previous_;
}

unsigned SetDeckCommand::changes() const
{
    return Change::Outdoor;
}

Id SetDeckCommand::floorId() const
{
    return floorId_;
}

// Folds the next change of the same deck and key into this one, so a drag undoes in one step.
bool SetDeckCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetDeckCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->deckId_ != deckId_) return false;
    values_ = other->values_;
    return true;
}

RemoveDeckCommand::RemoveDeckCommand(Id deckId) : deckId_(std::move(deckId)) {}

std::string RemoveDeckCommand::name() const
{
    return "Remove deck";
}

// Takes the deck out of its floor, remembering where it was, and lets the railings on it stand on their base.
Status RemoveDeckCommand::apply(Project& project)
{
    const Located found = locate(project, deckId_);
    if (!found.deck) return noDeck(deckId_);
    floorId_ = found.floor->id;
    removed_ = *found.deck;
    index_ = static_cast<std::size_t>(found.deck - found.floor->decks.data());
    hosted_.clear();
    for (Railing& railing : found.floor->railings) {
        if (railing.host && *railing.host == deckId_) {
            hosted_.push_back(railing.id);
            railing.host.reset();
        }
    }
    found.floor->decks.erase(found.floor->decks.begin() + static_cast<std::ptrdiff_t>(index_));
    return {};
}

// Puts the deck back in its old place and the railings back on it.
void RemoveDeckCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    const std::size_t at = std::min(index_, floor->decks.size());
    floor->decks.insert(floor->decks.begin() + static_cast<std::ptrdiff_t>(at), removed_);
    for (const Id& id : hosted_) {
        if (Railing* railing = findRailing(*floor, id)) railing->host = deckId_;
    }
}

unsigned RemoveDeckCommand::changes() const
{
    return Change::Outdoor;
}

Id RemoveDeckCommand::floorId() const
{
    return floorId_;
}

}
