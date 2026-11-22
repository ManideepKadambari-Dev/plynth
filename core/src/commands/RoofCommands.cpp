#include "plynth/core/commands/RoofCommands.h"

#include "CommandSupport.h"
#include "plynth/core/Roofs.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace plynth {

namespace {

// Checks the material name when one is set.
Status checkMaterial(const std::optional<std::string>& material)
{
    if (material && !detail::isMaterialName(*material)) {
        return fail(ErrorCode::InvalidValue, "The roof material \"" + *material + "\" is not a valid name.");
    }
    return {};
}

// The refusal for an entry that is not there.
Status noSection(const Id& id)
{
    return fail(ErrorCode::NotFound, "There is no roof section entry " + id + ".");
}

}

SetRoofDefaultCommand::SetRoofDefaultCommand(RoofSettings settings) : settings_(std::move(settings)) {}

std::string SetRoofDefaultCommand::name() const
{
    return "Change roof default";
}

// Checks the settings and makes them the plan's default.
Status SetRoofDefaultCommand::apply(Project& project)
{
    if (const Status s = checkRoofSettings(settings_); !s.ok()) return s;
    previous_ = project.plan.roof;
    project.plan.roof = settings_;
    return {};
}

// Puts the old default back.
void SetRoofDefaultCommand::undo(Project& project)
{
    project.plan.roof = previous_;
}

unsigned SetRoofDefaultCommand::changes() const
{
    return Change::Roofs;
}

AddRoofSectionCommand::AddRoofSectionCommand(Id floorId, double x, double y, RoofSettings settings,
                                             std::optional<std::string> material)
{
    section_.floor = std::move(floorId);
    section_.x = x;
    section_.y = y;
    section_.settings = std::move(settings);
    section_.material = std::move(material);
}

// The id of the new entry, empty until the first apply.
const Id& AddRoofSectionCommand::newSectionId() const
{
    return section_.id;
}

std::string AddRoofSectionCommand::name() const
{
    return "Add roof section settings";
}

// Checks the point and the settings and adds the entry.
Status AddRoofSectionCommand::apply(Project& project)
{
    if (!findFloor(project.plan, section_.floor)) {
        return fail(ErrorCode::NotFound, "There is no floor " + section_.floor + ".");
    }
    if (!(std::isfinite(section_.x) && std::isfinite(section_.y))) {
        return fail(ErrorCode::InvalidValue, "The roof section position must be finite numbers.");
    }
    const std::optional<RoofArea> area = roofAreaAt(project.plan, section_.floor, {section_.x, section_.y});
    if (!area) return fail(ErrorCode::InvalidValue, "There is no roof section there.");
    if (!area->section.empty()) {
        return fail(ErrorCode::AlreadyExists, "The roof section there already has the entry " + area->section + ".");
    }
    if (const Status s = checkRoofSettings(section_.settings); !s.ok()) return s;
    if (const Status s = checkMaterial(section_.material); !s.ok()) return s;
    if (section_.id.empty()) {
        section_.id = newId(project.plan, kRoofSectionIdPrefix);
    } else if (idInUse(project.plan, section_.id)) {
        return fail(ErrorCode::AlreadyExists, "The id " + section_.id + " is already used.");
    }
    project.plan.roofSections.push_back(section_);
    return {};
}

// Removes the entry that apply added.
void AddRoofSectionCommand::undo(Project& project)
{
    std::erase_if(project.plan.roofSections, [&](const RoofSection& r) { return r.id == section_.id; });
}

unsigned AddRoofSectionCommand::changes() const
{
    return Change::Roofs;
}

Id AddRoofSectionCommand::floorId() const
{
    return section_.floor;
}

SetRoofSectionCommand::SetRoofSectionCommand(Id sectionId, RoofSettings settings, std::optional<std::string> material)
    : sectionId_(std::move(sectionId)), settings_(std::move(settings)), material_(std::move(material))
{
}

std::string SetRoofSectionCommand::name() const
{
    return "Change roof section";
}

// Checks the values and replaces the entry's settings and material.
Status SetRoofSectionCommand::apply(Project& project)
{
    RoofSection* entry = findRoofSection(project.plan, sectionId_);
    if (!entry) return noSection(sectionId_);
    if (const Status s = checkRoofSettings(settings_); !s.ok()) return s;
    if (const Status s = checkMaterial(material_); !s.ok()) return s;
    previous_ = *entry;
    entry->settings = settings_;
    entry->material = material_;
    return {};
}

// Puts the entry back as it was.
void SetRoofSectionCommand::undo(Project& project)
{
    if (RoofSection* entry = findRoofSection(project.plan, sectionId_)) *entry = previous_;
}

unsigned SetRoofSectionCommand::changes() const
{
    return Change::Roofs;
}

Id SetRoofSectionCommand::floorId() const
{
    return previous_.floor;
}

RemoveRoofSectionCommand::RemoveRoofSectionCommand(Id sectionId) : sectionId_(std::move(sectionId)) {}

std::string RemoveRoofSectionCommand::name() const
{
    return "Remove roof section settings";
}

// Removes the entry, keeping it and its place for undo.
Status RemoveRoofSectionCommand::apply(Project& project)
{
    auto& list = project.plan.roofSections;
    const auto it = std::find_if(list.begin(), list.end(), [&](const RoofSection& r) { return r.id == sectionId_; });
    if (it == list.end()) return noSection(sectionId_);
    removed_ = *it;
    index_ = static_cast<std::size_t>(it - list.begin());
    list.erase(it);
    return {};
}

// Puts the entry back where it was.
void RemoveRoofSectionCommand::undo(Project& project)
{
    auto& list = project.plan.roofSections;
    list.insert(list.begin() + static_cast<std::ptrdiff_t>(std::min(index_, list.size())), removed_);
}

unsigned RemoveRoofSectionCommand::changes() const
{
    return Change::Roofs;
}

Id RemoveRoofSectionCommand::floorId() const
{
    return removed_.floor;
}

}
