#include "plynth/core/commands/OpeningCommands.h"

#include "plynth/core/Openings.h"

#include "CommandSupport.h"

#include <algorithm>
#include <utility>

namespace plynth {

AddOpeningCommand::AddOpeningCommand(Opening opening) : opening_(std::move(opening))
{
    opening_.id.clear();
}

// The id of the new opening, empty until the first apply.
const Id& AddOpeningCommand::openingId() const
{
    return opening_.id;
}

std::string AddOpeningCommand::name() const
{
    return "Add opening";
}

// Adds the opening after checking it fits, with a new id on the first call and the same id on redo.
Status AddOpeningCommand::apply(Project& project)
{
    Floor* floor = detail::floorOfWall(project.plan, opening_.wall);
    if (!floor) return fail(ErrorCode::NotFound, "There is no wall " + opening_.wall + ".");
    Opening candidate = opening_;
    if (candidate.id.empty()) {
        candidate.id = newId(project.plan, kOpeningIdPrefix);
    } else if (idInUse(project.plan, candidate.id)) {
        return fail(ErrorCode::AlreadyExists, "The id " + candidate.id + " is already used.");
    }
    if (Status s = checkOpeningFit(*floor, candidate); !s.ok()) return s;
    opening_.id = candidate.id;
    floorId_ = floor->id;
    floor->openings.push_back(std::move(candidate));
    return {};
}

// Removes the opening that apply added.
void AddOpeningCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    std::erase_if(floor->openings, [&](const Opening& o) { return o.id == opening_.id; });
}

unsigned AddOpeningCommand::changes() const
{
    return Change::Openings;
}

Id AddOpeningCommand::floorId() const
{
    return floorId_;
}

namespace {

// Finds the floor that holds an opening. Null when there is none.
Floor* floorOfOpening(Plan& plan, std::string_view openingId)
{
    for (Floor& f : plan.floors) {
        if (findOpening(f, openingId)) return &f;
    }
    return nullptr;
}

// Puts an opening back as it was.
void restore(Project& project, const Id& floorId, const Opening& previous)
{
    Floor* floor = findFloor(project.plan, floorId);
    if (!floor) return;
    if (Opening* o = findOpening(*floor, previous.id)) *o = previous;
}

// Looks up an opening for an edit, or says what is missing.
Status findForEdit(Project& project, const Id& openingId, Floor*& floor, Opening*& opening)
{
    floor = floorOfOpening(project.plan, openingId);
    if (!floor) return fail(ErrorCode::NotFound, "There is no opening " + openingId + ".");
    opening = findOpening(*floor, openingId);
    return {};
}

}

MoveOpeningCommand::MoveOpeningCommand(Id openingId, Id wallId, double offset, double sill, int mergeKey)
    : openingId_(std::move(openingId)), wallId_(std::move(wallId)), offset_(offset), sill_(sill), mergeKey_(mergeKey)
{
}

std::string MoveOpeningCommand::name() const
{
    return "Move opening";
}

// Moves the opening to the wall, offset and sill, after checking it fits there.
Status MoveOpeningCommand::apply(Project& project)
{
    Floor* floor = nullptr;
    Opening* opening = nullptr;
    if (Status s = findForEdit(project, openingId_, floor, opening); !s.ok()) return s;
    if (!findWall(*floor, wallId_)) {
        if (const Floor* other = findFloorContaining(project.plan, wallId_); other && other != floor) {
            return fail(ErrorCode::InvalidValue, "The wall " + wallId_ + " is on another floor.");
        }
        return fail(ErrorCode::NotFound, "There is no wall " + wallId_ + ".");
    }
    Opening candidate = *opening;
    candidate.wall = wallId_;
    candidate.offset = offset_;
    candidate.sill = sill_;
    if (Status s = checkOpeningFit(*floor, candidate); !s.ok()) return s;
    floorId_ = floor->id;
    previous_ = *opening;
    *opening = std::move(candidate);
    return {};
}

// Puts the opening back where it was.
void MoveOpeningCommand::undo(Project& project)
{
    restore(project, floorId_, previous_);
}

unsigned MoveOpeningCommand::changes() const
{
    return Change::Openings;
}

Id MoveOpeningCommand::floorId() const
{
    return floorId_;
}

// Takes over the later target of a move of the same opening in the same drag.
bool MoveOpeningCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const MoveOpeningCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->openingId_ != openingId_) return false;
    wallId_ = other->wallId_;
    offset_ = other->offset_;
    sill_ = other->sill_;
    return true;
}

ResizeOpeningCommand::ResizeOpeningCommand(Id openingId, double offset, double width, double sill, double height, int mergeKey)
    : openingId_(std::move(openingId)), offset_(offset), width_(width), sill_(sill), height_(height), mergeKey_(mergeKey)
{
}

std::string ResizeOpeningCommand::name() const
{
    return "Resize opening";
}

// Gives the opening its new box, after checking it fits.
Status ResizeOpeningCommand::apply(Project& project)
{
    Floor* floor = nullptr;
    Opening* opening = nullptr;
    if (Status s = findForEdit(project, openingId_, floor, opening); !s.ok()) return s;
    Opening candidate = *opening;
    candidate.offset = offset_;
    candidate.width = width_;
    candidate.sill = sill_;
    candidate.height = height_;
    if (Status s = checkOpeningFit(*floor, candidate); !s.ok()) return s;
    floorId_ = floor->id;
    previous_ = *opening;
    *opening = std::move(candidate);
    return {};
}

// Puts the old box back.
void ResizeOpeningCommand::undo(Project& project)
{
    restore(project, floorId_, previous_);
}

unsigned ResizeOpeningCommand::changes() const
{
    return Change::Openings;
}

Id ResizeOpeningCommand::floorId() const
{
    return floorId_;
}

// Takes over the later box of the same opening in the same drag.
bool ResizeOpeningCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const ResizeOpeningCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->openingId_ != openingId_) return false;
    offset_ = other->offset_;
    width_ = other->width_;
    sill_ = other->sill_;
    height_ = other->height_;
    return true;
}

SetOpeningStyleCommand::SetOpeningStyleCommand(Id openingId, OpeningStyle style)
    : openingId_(std::move(openingId)), style_(std::move(style))
{
}

std::string SetOpeningStyleCommand::name() const
{
    return "Change opening";
}

// Sets the type, shape, hinge and swing after checking the opening still fits.
Status SetOpeningStyleCommand::apply(Project& project)
{
    Floor* floor = nullptr;
    Opening* opening = nullptr;
    if (Status s = findForEdit(project, openingId_, floor, opening); !s.ok()) return s;
    Opening candidate = *opening;
    candidate.type = style_.type;
    candidate.shape = style_.shape;
    candidate.hinge = style_.hinge;
    candidate.swing = style_.swing;
    if (Status s = checkOpeningFit(*floor, candidate); !s.ok()) return s;
    floorId_ = floor->id;
    previous_ = {opening->type, opening->shape, opening->hinge, opening->swing};
    *opening = std::move(candidate);
    return {};
}

// Puts the old style back.
void SetOpeningStyleCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    if (Opening* o = findOpening(*floor, openingId_)) {
        o->type = previous_.type;
        o->shape = previous_.shape;
        o->hinge = previous_.hinge;
        o->swing = previous_.swing;
    }
}

unsigned SetOpeningStyleCommand::changes() const
{
    return Change::Openings;
}

Id SetOpeningStyleCommand::floorId() const
{
    return floorId_;
}

RemoveOpeningCommand::RemoveOpeningCommand(Id openingId) : openingId_(std::move(openingId)) {}

std::string RemoveOpeningCommand::name() const
{
    return "Remove opening";
}

// Removes the opening, remembering its place in the list.
Status RemoveOpeningCommand::apply(Project& project)
{
    Floor* floor = floorOfOpening(project.plan, openingId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no opening " + openingId_ + ".");
    const auto it = std::find_if(floor->openings.begin(), floor->openings.end(), [&](const Opening& o) { return o.id == openingId_; });
    floorId_ = floor->id;
    index_ = static_cast<std::size_t>(it - floor->openings.begin());
    removed_ = *it;
    floor->openings.erase(it);
    return {};
}

// Puts the opening back at its old place.
void RemoveOpeningCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    const std::size_t at = std::min(index_, floor->openings.size());
    floor->openings.insert(floor->openings.begin() + static_cast<std::ptrdiff_t>(at), removed_);
}

unsigned RemoveOpeningCommand::changes() const
{
    return Change::Openings;
}

Id RemoveOpeningCommand::floorId() const
{
    return floorId_;
}

}
