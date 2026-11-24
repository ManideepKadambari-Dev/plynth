#include <plynth/core/GeneratorBuild.h>

#include <plynth/core/ProjectFiles.h>

#include <algorithm>
#include <set>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// Makes a problem found before Blender starts.
BuildProblem problemFor(std::string code, BuildSeverity severity, std::string message, const Id& floor, const Id& object)
{
    BuildProblem p;
    p.code = std::move(code);
    p.severity = severity;
    p.message = std::move(message);
    p.floor = floor;
    p.object = object;
    p.step = std::string(kCheckStep);
    return p;
}

// The refusal of a library file whose stem is this generator id, or null.
const Status* refusalFor(const GeneratorLibrary& library, const std::string& id)
{
    for (const auto& [file, status] : library.refused) {
        const std::u8string stem = file.stem().u8string();
        if (std::string(stem.begin(), stem.end()) == id) return &status;
    }
    return nullptr;
}

// Joins notes into one sentence line.
std::string joined(const std::vector<std::string>& notes)
{
    std::string text;
    for (const std::string& note : notes) {
        if (!text.empty()) text += " ";
        text += note;
    }
    return text;
}

// One value as JSON. A material that is empty or not usable is null.
Json valueJson(const GeneratorValue& value, bool material, const MaterialUsable& usable)
{
    if (const double* number = std::get_if<double>(&value)) return *number;
    if (const bool* flag = std::get_if<bool>(&value)) return *flag;
    const std::string& text = std::get<std::string>(value);
    if (material && (text.empty() || (usable && !usable(text)))) return nullptr;
    return text;
}

}

// Works out the generator part of a build.
GeneratorBuildPlan makeGeneratorBuildPlan(const Plan& plan, const GeneratorLibrary* library,
                                          const std::optional<AddonCache>& addons)
{
    GeneratorBuildPlan out;
    const std::vector<double> levels = floorElevations(plan);
    std::set<std::string> materials;
    std::set<std::string> used;
    for (std::size_t f = 0; f < plan.floors.size(); ++f) {
        const Floor& floor = plan.floors[f];
        GeneratorFloorBuild built;
        built.floor = floor.id;
        for (const Item& item : floor.items) {
            if (!library || !isGeneratorItem(item)) continue;
            const GeneratorUse& use = *item.generator;
            const GeneratorInfo* info = library->find(use.name);
            if (!info) {
                if (const Status* refused = refusalFor(*library, use.name)) {
                    out.problems.push_back(problemFor(std::string(kGeneratorBrokenProblem), BuildSeverity::Error,
                                                      "The generator " + use.name + " of item " + item.id +
                                                          " could not be read: " + refused->message,
                                                      floor.id, item.id));
                } else {
                    out.problems.push_back(problemFor(std::string(kMissingGeneratorProblem), BuildSeverity::Error,
                                                      "The generator " + use.name + " of item " + item.id +
                                                          " is not in the library.",
                                                      floor.id, item.id));
                }
                continue;
            }
            const GeneratorState state = generatorState(*info, addons);
            if (state.availability == GeneratorAvailability::NeedsAddon) {
                out.problems.push_back(problemFor(std::string(kGeneratorUnavailableProblem), BuildSeverity::Warning,
                                                  info->name + " " + item.id + " was left out. " + state.message,
                                                  floor.id, item.id));
                continue;
            }

            const GeneratorParamsFit fit = fitGeneratorParams(*info, use.params, floor.ceilingHeight);
            if (fit.changed) {
                out.problems.push_back(problemFor(std::string(kGeneratorChangedProblem), BuildSeverity::Warning,
                                                  info->name + " " + item.id + " no longer matches its generator, so " +
                                                      "its values were adjusted. " + joined(fit.notes),
                                                  floor.id, item.id));
            }
            const GeneratorSize size = generatorSize(*info, fit.params);

            const ItemPlacement placement = itemPlacement(item, levels[f]);
            GeneratorItemBuild entry;
            entry.id = item.id;
            entry.generator = info->id;
            entry.params = fit.params;
            entry.materialParams = generatorMaterialParams(*info);
            entry.location = placement.location;
            entry.rotationZ = placement.rotationZ;
            entry.size = size.status.ok() ? size.size : item.size;
            entry.room = item.room;
            entry.wall = item.wall;
            for (const std::string& param : entry.materialParams) {
                const GeneratorValue* value = findGeneratorValue(entry.params, param);
                const std::string* text = value ? std::get_if<std::string>(value) : nullptr;
                if (text && !text->empty()) materials.insert(*text);
            }
            if (!info->addons.empty()) out.needsAddons = true;
            used.insert(info->id);
            built.items.push_back(std::move(entry));
        }
        out.floors.push_back(std::move(built));
    }
    for (const std::string& id : used) {
        const GeneratorInfo* info = library->find(id);
        out.generators.push_back({info->id, info->file, info->addons});
    }
    out.materials.assign(materials.begin(), materials.end());
    return out;
}

// The job's generators list.
Json generatorJobList(const GeneratorBuildPlan& plan)
{
    Json list = Json::array();
    for (const BuildGenerator& g : plan.generators) {
        Json entry = Json::object();
        entry["id"] = g.id;
        entry["file"] = pathToUtf8(g.file);
        entry["addons"] = g.addons;
        list.push_back(std::move(entry));
    }
    return list;
}

// One floor's generator_items list.
Json generatorJobItems(const GeneratorFloorBuild& floor, const MaterialUsable& usable)
{
    Json list = Json::array();
    for (const GeneratorItemBuild& item : floor.items) {
        Json params = Json::object();
        for (const GeneratorParamValue& v : item.params) {
            const bool material = std::find(item.materialParams.begin(), item.materialParams.end(), v.id) != item.materialParams.end();
            params[v.id] = valueJson(v.value, material, usable);
        }
        Json entry = Json::object();
        entry["id"] = item.id;
        entry["generator"] = item.generator;
        entry["params"] = std::move(params);
        entry["location"] = Json::array({item.location.x, item.location.y, item.location.z});
        entry["rotation_z"] = item.rotationZ;
        entry["size"] = Json{{"width", item.size.width}, {"depth", item.size.depth}, {"height", item.size.height}};
        entry["room"] = item.room ? Json(*item.room) : Json(nullptr);
        entry["wall"] = item.wall ? Json(*item.wall) : Json(nullptr);
        Json slots = Json::object();
        for (const GeneratorMaterialSlot& slot : item.materialSlots) slots[slot.param] = slot.slot;
        entry["slots"] = std::move(slots);
        list.push_back(std::move(entry));
    }
    return list;
}

}
