#include "plynth/core/commands/FloorCommands.h"

#include "plynth/core/Openings.h"

#include "CommandSupport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <utility>

namespace plynth {

SetFloorImageCommand::SetFloorImageCommand(Id floorId, std::optional<std::string> image)
    : floorId_(std::move(floorId)), image_(std::move(image))
{
}

std::string SetFloorImageCommand::name() const
{
    return image_ ? "Set plan image" : "Clear plan image";
}

// Sets or clears the image of the floor, remembering the old one.
Status SetFloorImageCommand::apply(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");
    if (image_ && image_->empty()) return fail(ErrorCode::InvalidValue, "The plan image path is empty.");
    previous_ = floor->image;
    floor->image = image_;
    return {};
}

// Puts the old image back.
void SetFloorImageCommand::undo(Project& project)
{
    if (Floor* floor = findFloor(project.plan, floorId_)) floor->image = previous_;
}

unsigned SetFloorImageCommand::changes() const
{
    return Change::FloorImage;
}

Id SetFloorImageCommand::floorId() const
{
    return floorId_;
}

SetScaleCommand::SetScaleCommand(std::optional<double> pixelsPerMetre) : pixelsPerMetre_(pixelsPerMetre) {}

SetScaleCommand::SetScaleCommand(std::optional<double> pixelsPerMetre, ScaleKeep keep)
    : pixelsPerMetre_(pixelsPerMetre), keep_(keep)
{
}

// True when the last apply moved marks.
bool SetScaleCommand::movedMarks() const
{
    return moved_;
}

std::string SetScaleCommand::name() const
{
    return pixelsPerMetre_ ? "Set scale" : "Clear scale";
}

// Sets or clears the scale. When an old scale changes and marks keep their image place, their metre values are
// rescaled and the floors are saved whole so undo is exact.
Status SetScaleCommand::apply(Project& project)
{
    if (pixelsPerMetre_ && !detail::isPositive(*pixelsPerMetre_)) {
        return fail(ErrorCode::InvalidValue, "The scale must be a number above zero.");
    }
    previous_ = project.plan.scale.pixelsPerMetre;
    moved_ = false;
    previousFloors_.clear();
    if (keep_ == ScaleKeep::ImagePositions && previous_ && pixelsPerMetre_ && *previous_ != *pixelsPerMetre_) {
        bool hasMarks = !project.plan.roofSections.empty();
        for (const Floor& f : project.plan.floors) {
            if (!f.points.empty() || !f.openings.empty() || !f.items.empty() || !f.stairs.empty() || !f.lights.empty()) {
                hasMarks = true;
            }
        }
        if (hasMarks) {
            const double factor = *previous_ / *pixelsPerMetre_;
            previousFloors_ = project.plan.floors;
            previousRoofSections_ = project.plan.roofSections;
            for (Floor& f : project.plan.floors) {
                const ImageOffset o = f.imageOffset;
                const auto about = [&](double& x, double& y) {
                    x = o.x + (x - o.x) * factor;
                    y = o.y + (y - o.y) * factor;
                };
                for (Point& p : f.points) about(p.x, p.y);
                for (Item& i : f.items) about(i.x, i.y);
                for (Stair& s : f.stairs) about(s.x, s.y);
                for (LightMarker& l : f.lights) about(l.x, l.y);
                for (Wall& w : f.walls) w.bulge *= factor;
                for (Opening& op : f.openings) op.offset *= factor;
            }
            for (RoofSection& r : project.plan.roofSections) {
                const Floor* f = findFloor(project.plan, r.floor);
                const ImageOffset o = f ? f->imageOffset : ImageOffset{};
                r.x = o.x + (r.x - o.x) * factor;
                r.y = o.y + (r.y - o.y) * factor;
            }
            moved_ = true;
        }
    }
    project.plan.scale.pixelsPerMetre = pixelsPerMetre_;
    return {};
}

// Puts the old scale back, and the old floors when marks were moved.
void SetScaleCommand::undo(Project& project)
{
    project.plan.scale.pixelsPerMetre = previous_;
    if (moved_) {
        project.plan.floors = previousFloors_;
        project.plan.roofSections = previousRoofSections_;
    }
}

unsigned SetScaleCommand::changes() const
{
    return moved_ ? (Change::Scale | Change::Points | Change::Walls | Change::Openings | Change::Items | Change::Stairs | Change::Roofs)
                  : Change::Scale;
}

}

namespace plynth {

SetCeilingHeightCommand::SetCeilingHeightCommand(Id floorId, double height, int mergeKey)
    : floorId_(std::move(floorId)), height_(height), mergeKey_(mergeKey)
{
}

std::string SetCeilingHeightCommand::name() const
{
    return "Set ceiling height";
}

// Sets the ceiling height unless an opening on a wall that follows it would end up above the wall.
Status SetCeilingHeightCommand::apply(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");
    if (!detail::isPositive(height_)) {
        return fail(ErrorCode::InvalidValue, "The ceiling height must be a number above zero.");
    }
    for (const Opening& o : floor->openings) {
        const Wall* wall = findWall(*floor, o.wall);
        if (!wall || wall->height) continue;
        if (o.sill + o.height > height_ + kOpeningTouchTolerance) {
            char text[48];
            std::snprintf(text, sizeof text, "%.2f m", height_);
            return fail(ErrorCode::InvalidValue,
                        "The opening " + o.id + " would reach above the wall " + wall->id + ", which would be " + text + " high.");
        }
    }
    previous_ = floor->ceilingHeight;
    floor->ceilingHeight = height_;
    return {};
}

// Puts the old ceiling height back.
void SetCeilingHeightCommand::undo(Project& project)
{
    if (Floor* floor = findFloor(project.plan, floorId_)) floor->ceilingHeight = previous_;
}

unsigned SetCeilingHeightCommand::changes() const
{
    return Change::Floors;
}

Id SetCeilingHeightCommand::floorId() const
{
    return floorId_;
}

// Takes over the later height of the same floor in the same series of changes.
bool SetCeilingHeightCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetCeilingHeightCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->floorId_ != floorId_) return false;
    height_ = other->height_;
    return true;
}

}

namespace plynth {

namespace {

// A name without the spaces at its ends.
std::string trimmed(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

}

AddFloorCommand::AddFloorCommand(std::string name, std::optional<std::size_t> position)
    : name_(std::move(name)), position_(position)
{
}

// The id the floor got.
const Id& AddFloorCommand::newFloorId() const
{
    return id_;
}

std::string AddFloorCommand::name() const
{
    return "Add floor";
}

// Adds an empty floor at the wanted place in plan order.
Status AddFloorCommand::apply(Project& project)
{
    const std::string text = trimmed(name_);
    if (text.empty()) return fail(ErrorCode::InvalidValue, "The floor name is empty.");
    std::vector<Floor>& floors = project.plan.floors;
    if (position_ && *position_ > floors.size()) {
        return fail(ErrorCode::InvalidValue, "The floor position is past the top floor.");
    }
    if (id_.empty()) id_ = newId(project.plan, kFloorIdPrefix);
    if (idInUse(project.plan, id_)) return fail(ErrorCode::AlreadyExists, "The id " + id_ + " is already used.");
    Floor floor;
    floor.id = id_;
    floor.name = text;
    floor.ceilingHeight = project.settings.defaults.ceilingHeight;
    floor.slabThickness = kDefaultSlabThickness;
    const std::size_t at = position_ ? *position_ : floors.size();
    floors.insert(floors.begin() + static_cast<std::ptrdiff_t>(at), std::move(floor));
    return {};
}

// Takes the floor out again.
void AddFloorCommand::undo(Project& project)
{
    std::vector<Floor>& floors = project.plan.floors;
    for (auto it = floors.begin(); it != floors.end(); ++it) {
        if (it->id == id_) {
            floors.erase(it);
            return;
        }
    }
}

unsigned AddFloorCommand::changes() const
{
    return Change::Floors;
}

Id AddFloorCommand::floorId() const
{
    return id_;
}

RemoveFloorCommand::RemoveFloorCommand(Id floorId) : floorId_(std::move(floorId)) {}

std::string RemoveFloorCommand::name() const
{
    return "Remove floor";
}

// Removes the floor and the roof entries tied to it, keeping both for undo.
Status RemoveFloorCommand::apply(Project& project)
{
    std::vector<Floor>& floors = project.plan.floors;
    const auto index = floorIndex(project.plan, floorId_);
    if (!index) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");
    if (floors.size() == 1) return fail(ErrorCode::InvalidValue, "The only floor cannot be removed.");
    index_ = *index;
    removed_ = floors[index_];
    floors.erase(floors.begin() + static_cast<std::ptrdiff_t>(index_));
    removedSections_.clear();
    std::vector<RoofSection>& sections = project.plan.roofSections;
    for (std::size_t i = 0; i < sections.size();) {
        if (sections[i].floor == floorId_) {
            removedSections_.emplace_back(i + removedSections_.size(), sections[i]);
            sections.erase(sections.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    return {};
}

// Puts the floor and its roof entries back where they were.
void RemoveFloorCommand::undo(Project& project)
{
    if (!removed_) return;
    std::vector<Floor>& floors = project.plan.floors;
    floors.insert(floors.begin() + static_cast<std::ptrdiff_t>(std::min(index_, floors.size())), *removed_);
    std::vector<RoofSection>& sections = project.plan.roofSections;
    for (const auto& [at, section] : removedSections_) {
        sections.insert(sections.begin() + static_cast<std::ptrdiff_t>(std::min(at, sections.size())), section);
    }
}

unsigned RemoveFloorCommand::changes() const
{
    return Change::Floors | Change::Points | Change::Walls | Change::Rooms | Change::Openings | Change::Items | Change::Stairs |
           Change::Roofs;
}

SetFloorNameCommand::SetFloorNameCommand(Id floorId, std::string name) : floorId_(std::move(floorId)), name_(std::move(name)) {}

std::string SetFloorNameCommand::name() const
{
    return "Rename floor";
}

// Gives the floor its new name.
Status SetFloorNameCommand::apply(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");
    const std::string text = trimmed(name_);
    if (text.empty()) return fail(ErrorCode::InvalidValue, "The floor name is empty.");
    previous_ = floor->name;
    floor->name = text;
    return {};
}

// Puts the old name back.
void SetFloorNameCommand::undo(Project& project)
{
    if (Floor* floor = findFloor(project.plan, floorId_)) floor->name = previous_;
}

unsigned SetFloorNameCommand::changes() const
{
    return Change::Floors;
}

Id SetFloorNameCommand::floorId() const
{
    return floorId_;
}

SetSlabThicknessCommand::SetSlabThicknessCommand(Id floorId, double thickness, int mergeKey)
    : floorId_(std::move(floorId)), thickness_(thickness), mergeKey_(mergeKey)
{
}

std::string SetSlabThicknessCommand::name() const
{
    return "Set slab thickness";
}

// Sets the slab thickness of the floor.
Status SetSlabThicknessCommand::apply(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");
    if (!std::isfinite(thickness_) || thickness_ < kMinSlabThickness || thickness_ > kMaxSlabThickness) {
        return fail(ErrorCode::InvalidValue, "The slab thickness must be from 0.05 to 1 metre.");
    }
    previous_ = floor->slabThickness;
    floor->slabThickness = thickness_;
    return {};
}

// Puts the old thickness back.
void SetSlabThicknessCommand::undo(Project& project)
{
    if (Floor* floor = findFloor(project.plan, floorId_)) floor->slabThickness = previous_;
}

unsigned SetSlabThicknessCommand::changes() const
{
    return Change::Floors;
}

Id SetSlabThicknessCommand::floorId() const
{
    return floorId_;
}

// Takes over the later thickness of the same floor in the same series of changes.
bool SetSlabThicknessCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetSlabThicknessCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->floorId_ != floorId_) return false;
    thickness_ = other->thickness_;
    return true;
}

SetFloorImageOffsetCommand::SetFloorImageOffsetCommand(Id floorId, ImageOffset offset, bool moveMarks, int mergeKey)
    : floorId_(std::move(floorId)), offset_(offset), moveMarks_(moveMarks), mergeKey_(mergeKey)
{
}

std::string SetFloorImageOffsetCommand::name() const
{
    return "Move plan image";
}

// Moves the image offset and, when asked, every mark of the floor by the same amount.
Status SetFloorImageOffsetCommand::apply(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");
    if (!std::isfinite(offset_.x) || !std::isfinite(offset_.y)) {
        return fail(ErrorCode::InvalidValue, "The image offset must be a finite number.");
    }
    previousFloor_ = *floor;
    previousSections_ = project.plan.roofSections;
    const double dx = offset_.x - floor->imageOffset.x;
    const double dy = offset_.y - floor->imageOffset.y;
    floor->imageOffset = offset_;
    if (moveMarks_) {
        for (Point& p : floor->points) {
            p.x += dx;
            p.y += dy;
        }
        for (Item& i : floor->items) {
            i.x += dx;
            i.y += dy;
        }
        for (Stair& s : floor->stairs) {
            s.x += dx;
            s.y += dy;
        }
        for (LightMarker& l : floor->lights) {
            l.x += dx;
            l.y += dy;
        }
        for (RoofSection& r : project.plan.roofSections) {
            if (r.floor != floorId_) continue;
            r.x += dx;
            r.y += dy;
        }
    }
    return {};
}

// Puts the floor and the roof entries back exactly as they were.
void SetFloorImageOffsetCommand::undo(Project& project)
{
    if (Floor* floor = findFloor(project.plan, floorId_)) *floor = previousFloor_;
    project.plan.roofSections = previousSections_;
}

unsigned SetFloorImageOffsetCommand::changes() const
{
    unsigned flags = Change::FloorImage;
    if (moveMarks_) flags |= Change::Points | Change::Walls | Change::Openings | Change::Items | Change::Stairs | Change::Roofs;
    return flags;
}

Id SetFloorImageOffsetCommand::floorId() const
{
    return floorId_;
}

// Takes over the later offset of the same floor in the same series of changes.
bool SetFloorImageOffsetCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetFloorImageOffsetCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->floorId_ != floorId_ || other->moveMarks_ != moveMarks_) {
        return false;
    }
    offset_ = other->offset_;
    return true;
}

}
