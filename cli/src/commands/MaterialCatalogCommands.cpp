#include "../CatalogCommon.h"
#include "../Common.h"

#include <plynth/core/catalog/MaterialInfo.h>

#include <algorithm>
#include <memory>
#include <optional>

namespace plynth::cli {

namespace {

struct FilterOptions {
    std::string library;
    std::string text;
    std::string group;
    std::vector<std::string> tags;
    std::string license;
    bool uncategorized = false;
    int limit = 0;
};

struct IdOptions {
    std::string library;
    std::string id;
};

struct RemoveOptions {
    std::string library;
    std::string id;
    bool yes = false;
};

struct SetOptions {
    std::string library;
    std::string id;
    std::optional<std::string> name;
    std::vector<std::string> groups;
    bool noGroups = false;
    std::vector<std::string> tags;
    std::optional<std::string> license;
    std::optional<std::string> tile;
};

struct GroupAddOptions {
    std::string library;
    std::string id;
    std::string name;
};

struct GroupSetOptions {
    std::string library;
    std::string id;
    std::optional<std::string> name;
};

// The unknown-id answer shared by the lookups below.
Answer notFound(const std::string& command, const std::string& what, const std::string& id) {
    return failure(command, fail(ErrorCode::NotFound, "There is no " + what + " " + id + "."));
}

// A query from the filter options. Fails with not_found when a group or license is unknown.
bool buildQuery(const std::string& command, const OpenedMaterialCatalog& opened, const FilterOptions& options,
                MaterialQuery& query, Answer& failed) {
    query.text = options.text;
    if (!options.group.empty()) {
        const auto groups = opened.catalog->groups();
        if (std::ranges::none_of(groups, [&](const MaterialGroupEntry& item) { return item.id == options.group; })) {
            failed = notFound(command, "group", options.group);
            return false;
        }
        query.group = options.group;
    }
    if (!options.license.empty()) {
        const auto licenses = opened.catalog->licenses();
        if (std::ranges::none_of(licenses, [&](const LicenseEntry& item) { return item.id == options.license; })) {
            failed = notFound(command, "license", options.license);
            return false;
        }
        query.license = options.license;
    }
    query.tags = options.tags;
    query.uncategorizedOnly = options.uncategorized;
    if (options.limit < 0) {
        failed = failure(command, fail(ErrorCode::InvalidValue, "The limit cannot be negative."));
        return false;
    }
    query.limit = options.limit;
    return true;
}

// Runs a search or list.
Answer materialsSearch(const std::string& command, const RunContext& context, const FilterOptions& options) {
    OpenedMaterialCatalog opened;
    Answer failed;
    if (!openMaterialCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    MaterialQuery query;
    if (!buildQuery(command, opened, options, query, failed)) {
        return failed;
    }
    Json materials = Json::array();
    for (const MaterialRecord& record : opened.catalog->search(query)) {
        materials.push_back(materialEntry(record));
    }
    Json result = Json::object();
    const std::size_t count = materials.size();
    result["materials"] = std::move(materials);
    result["count"] = count;
    return success(command, std::move(result));
}

// One material in full: its entry plus the maps with absolute paths, the texture size and the warnings.
Answer materialsInfo(const RunContext& context, const IdOptions& options) {
    const std::string command = "materials info";
    Answer badId;
    if (!checkIdArgument(command, options.id, badId)) {
        return badId;
    }
    OpenedMaterialCatalog opened;
    Answer failed;
    if (!openMaterialCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    const auto record = opened.catalog->material(options.id);
    if (!record) {
        return notFound(command, "material", options.id);
    }
    const MaterialInfoRead read = opened.catalog->info(options.id);
    if (!read.status.ok()) {
        return failure(command, read.status);
    }
    Json maps = Json::object();
    for (const MapChannel channel : allMapChannels()) {
        const MaterialMap& map = materialMap(read.info.maps, channel);
        Json item = Json::object();
        if (map.file.empty()) {
            item["file"] = nullptr;
        } else {
            item["file"] = pathToUtf8((record->folder / pathFromUtf8(map.file)).lexically_normal());
        }
        item["from"] = std::string(mapOriginName(map.origin));
        if (map.detail.empty()) {
            item["detail"] = nullptr;
        } else {
            item["detail"] = map.detail;
        }
        maps[std::string(mapChannelName(channel))] = std::move(item);
    }
    Json size = Json::object();
    size["width"] = read.info.textureSize.width;
    size["height"] = read.info.textureSize.height;
    Json warnings = Json::array();
    for (const BlenderWarning& warning : read.info.warnings) {
        Json item = Json::object();
        item["code"] = warning.code;
        item["message"] = warning.message;
        warnings.push_back(std::move(item));
    }
    Json entry = materialEntry(*record);
    entry["maps"] = std::move(maps);
    entry["texture_size"] = std::move(size);
    entry["warnings"] = std::move(warnings);
    return success(command, std::move(entry));
}

// Changes the name, groups, tags, license and tile size of a material.
Answer materialsSet(const RunContext& context, const SetOptions& options) {
    const std::string command = "materials set";
    Answer badId;
    if (!checkIdArgument(command, options.id, badId)) {
        return badId;
    }
    if (options.noGroups && !options.groups.empty()) {
        return badArguments(command, "bad_arguments", "Give --group or --no-groups, not both.");
    }
    const bool groupChange = options.noGroups || !options.groups.empty();
    if (!options.name && !groupChange && options.tags.empty() && !options.license && !options.tile) {
        return badArguments(command, "bad_arguments", "Give at least one option to change. Run 'plynth help'.");
    }
    MaterialEdit edit;
    edit.name = options.name;
    edit.license = options.license;
    if (groupChange) {
        edit.groups = options.groups;
    }
    if (!options.tags.empty()) {
        edit.tags = options.tags;
    }
    if (options.tile) {
        TileSize tile;
        const Status parsed = parseTileSize(*options.tile, tile);
        if (!parsed.ok()) {
            return failure(command, parsed);
        }
        edit.tileSize = tile;
    }
    OpenedMaterialCatalog opened;
    Answer failed;
    if (!openMaterialCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    if (!opened.catalog->material(options.id)) {
        return notFound(command, "material", options.id);
    }
    const Status status = opened.catalog->editMaterial(options.id, edit);
    if (!status.ok()) {
        return failure(command, status);
    }
    return success(command, materialEntry(*opened.catalog->material(options.id)));
}

// Deletes a material's folder and index row. Needs --yes.
Answer materialsRemove(const RunContext& context, const RemoveOptions& options) {
    const std::string command = "materials remove";
    Answer badId;
    if (!checkIdArgument(command, options.id, badId)) {
        return badId;
    }
    if (!options.yes) {
        return badArguments(command, "bad_arguments",
                            "Removing a material deletes its folder and cannot be undone. Add --yes to confirm.");
    }
    OpenedMaterialCatalog opened;
    Answer failed;
    if (!openMaterialCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    const Status status = opened.catalog->removeMaterial(options.id);
    if (!status.ok()) {
        return failure(command, status);
    }
    Json result = Json::object();
    result["removed"] = options.id;
    return success(command, std::move(result));
}

// Lists the groups with how many materials hold each.
Answer materialsGroups(const RunContext& context, const std::string& library) {
    const std::string command = "materials groups";
    OpenedMaterialCatalog opened;
    Answer failed;
    if (!openMaterialCatalog(command, context, library, opened, failed)) {
        return failed;
    }
    Json groups = Json::array();
    for (const MaterialGroupEntry& entry : opened.catalog->groups()) {
        groups.push_back(materialGroupEntry(entry));
    }
    Json result = Json::object();
    result["groups"] = std::move(groups);
    return success(command, std::move(result));
}

// The entry of one group, after a change.
Answer groupEntryAnswer(const std::string& command, const MaterialCatalog& catalog, const std::string& id) {
    for (const MaterialGroupEntry& entry : catalog.groups()) {
        if (entry.id == id) {
            return success(command, materialGroupEntry(entry));
        }
    }
    return notFound(command, "group", id);
}

// Adds a group.
Answer groupAdd(const RunContext& context, const GroupAddOptions& options) {
    const std::string command = "materials group add";
    Answer badId;
    if (!checkIdArgument(command, options.id, badId)) {
        return badId;
    }
    OpenedMaterialCatalog opened;
    Answer failed;
    if (!openMaterialCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    const Status status = opened.catalog->addGroup(options.id, options.name);
    if (!status.ok()) {
        return failure(command, status);
    }
    return groupEntryAnswer(command, *opened.catalog, options.id);
}

// Renames a group.
Answer groupSet(const RunContext& context, const GroupSetOptions& options) {
    const std::string command = "materials group set";
    Answer badId;
    if (!checkIdArgument(command, options.id, badId)) {
        return badId;
    }
    if (!options.name) {
        return badArguments(command, "bad_arguments", "Give at least one option to change. Run 'plynth help'.");
    }
    OpenedMaterialCatalog opened;
    Answer failed;
    if (!openMaterialCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    const Status status = opened.catalog->renameGroup(options.id, *options.name);
    if (!status.ok()) {
        return failure(command, status);
    }
    return groupEntryAnswer(command, *opened.catalog, options.id);
}

// Removes a group and says which materials lost it.
Answer groupRemove(const RunContext& context, const IdOptions& options) {
    const std::string command = "materials group remove";
    Answer badId;
    if (!checkIdArgument(command, options.id, badId)) {
        return badId;
    }
    OpenedMaterialCatalog opened;
    Answer failed;
    if (!openMaterialCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    MaterialQuery query;
    query.group = options.id;
    std::vector<std::string> changed;
    for (const MaterialRecord& record : opened.catalog->search(query)) {
        changed.push_back(record.id);
    }
    const Status status = opened.catalog->removeGroup(options.id);
    if (!status.ok()) {
        return failure(command, status);
    }
    Json result = Json::object();
    result["removed"] = options.id;
    result["materials_changed"] = changed;
    return success(command, std::move(result));
}

// Lists the licenses.
Answer materialsLicenses(const RunContext& context, const std::string& library) {
    const std::string command = "materials licenses";
    OpenedMaterialCatalog opened;
    Answer failed;
    if (!openMaterialCatalog(command, context, library, opened, failed)) {
        return failed;
    }
    Json licenses = Json::array();
    for (const LicenseEntry& entry : opened.catalog->licenses()) {
        licenses.push_back(licenseEntry(entry));
    }
    Json result = Json::object();
    result["licenses"] = std::move(licenses);
    return success(command, std::move(result));
}

// Rebuilds the index from the folders. Blender is not run: changed materials are listed in needs_refresh.
Answer materialsRescan(const RunContext& context, const std::string& library) {
    const std::string command = "materials rescan";
    OpenedMaterialCatalog opened;
    Answer failed;
    if (!openMaterialCatalog(command, context, library, opened, failed)) {
        return failed;
    }
    return success(command, rescanEntry(opened.catalog->rescan(), {}, Json::array()));
}

// Adds the filter options shared by materials search and materials list.
void addFilterOptions(CLI::App& sub, FilterOptions& options) {
    addLibraryOption(sub, options.library);
    sub.add_option("--group", options.group, "Only materials in this group.")->type_name("id");
    sub.add_option("--tag", options.tags, "Only materials with this tag. Repeat to require several.")
        ->type_name("tag");
    sub.add_option("--license", options.license, "Only materials with this license.")->type_name("id");
    sub.add_flag("--uncategorized", options.uncategorized, "Only materials without a group.");
    sub.add_option("--limit", options.limit, "Show at most this many materials.")->type_name("number");
}

// Adds the library option and the id argument shared by the id commands.
void addIdOptions(CLI::App& sub, IdOptions& options, const std::string& what) {
    addLibraryOption(sub, options.library);
    sub.add_option("id", options.id, "Id of the " + what + ".")->type_name("id")->required();
}

}

std::vector<CommandSpec> materialCatalogCommands() {
    CommandSpec search;
    search.words = {"materials", "search"};
    search.description = "Search the materials by words and filters.";
    search.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<FilterOptions>();
        sub.add_option("text", options->text, "Words to find at the start of a name, id, tag or group.")
            ->type_name("text");
        addFilterOptions(sub, *options);
        return [options](const RunContext& context) { return materialsSearch("materials search", context, *options); };
    };

    CommandSpec list;
    list.words = {"materials", "list"};
    list.description = "List the materials, with the same filters as search.";
    list.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<FilterOptions>();
        addFilterOptions(sub, *options);
        return [options](const RunContext& context) { return materialsSearch("materials list", context, *options); };
    };

    CommandSpec info;
    info.words = {"materials", "info"};
    info.description = "Show one material with its maps, texture size and warnings.";
    info.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<IdOptions>();
        addIdOptions(sub, *options, "material");
        return [options](const RunContext& context) { return materialsInfo(context, *options); };
    };

    CommandSpec set;
    set.words = {"materials", "set"};
    set.description = "Change a material's name, groups, tags, license or tile size.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SetOptions>();
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the material.")->type_name("id")->required();
        sub.add_option("--name", options->name, "New name.")->type_name("text");
        sub.add_option("--group", options->groups, "A group. Repeat to give several. Replaces the groups.")
            ->type_name("id");
        sub.add_flag("--no-groups", options->noGroups, "Take the material out of every group.");
        sub.add_option("--tag", options->tags, "A tag. Repeat to give several. Replaces the tags.")->type_name("tag");
        sub.add_option("--license", options->license, "License id.")->type_name("id");
        sub.add_option("--tile", options->tile, "Real world size of one tile in metres, 1.2x0.6 or 2.")
            ->type_name("WxH");
        return [options](const RunContext& context) { return materialsSet(context, *options); };
    };

    CommandSpec remove;
    remove.words = {"materials", "remove"};
    remove.description = "Delete a material and its folder. Needs --yes.";
    remove.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RemoveOptions>();
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the material.")->type_name("id")->required();
        sub.add_flag("--yes", options->yes, "Confirm the deletion.");
        return [options](const RunContext& context) { return materialsRemove(context, *options); };
    };

    CommandSpec groups;
    groups.words = {"materials", "groups"};
    groups.description = "List the groups with how many materials hold each.";
    groups.setup = [](CLI::App& sub) -> Handler {
        auto library = std::make_shared<std::string>();
        addLibraryOption(sub, *library);
        return [library](const RunContext& context) { return materialsGroups(context, *library); };
    };

    CommandSpec groupAddSpec;
    groupAddSpec.words = {"materials", "group", "add"};
    groupAddSpec.description = "Add a group.";
    groupAddSpec.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<GroupAddOptions>();
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the new group.")->type_name("id")->required();
        sub.add_option("name", options->name, "Name shown to the user.")->type_name("text")->required();
        return [options](const RunContext& context) { return groupAdd(context, *options); };
    };

    CommandSpec groupSetSpec;
    groupSetSpec.words = {"materials", "group", "set"};
    groupSetSpec.description = "Rename a group.";
    groupSetSpec.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<GroupSetOptions>();
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the group.")->type_name("id")->required();
        sub.add_option("--name", options->name, "New name.")->type_name("text");
        return [options](const RunContext& context) { return groupSet(context, *options); };
    };

    CommandSpec groupRemoveSpec;
    groupRemoveSpec.words = {"materials", "group", "remove"};
    groupRemoveSpec.description = "Remove a group from the list and from every material that has it.";
    groupRemoveSpec.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<IdOptions>();
        addIdOptions(sub, *options, "group");
        return [options](const RunContext& context) { return groupRemove(context, *options); };
    };

    CommandSpec licenses;
    licenses.words = {"materials", "licenses"};
    licenses.description = "List the licenses a material can have.";
    licenses.setup = [](CLI::App& sub) -> Handler {
        auto library = std::make_shared<std::string>();
        addLibraryOption(sub, *library);
        return [library](const RunContext& context) { return materialsLicenses(context, *library); };
    };

    CommandSpec rescan;
    rescan.words = {"materials", "rescan"};
    rescan.description = "Rebuild the materials index from the folders. Lists the materials that need a new render.";
    rescan.setup = [](CLI::App& sub) -> Handler {
        auto library = std::make_shared<std::string>();
        addLibraryOption(sub, *library);
        return [library](const RunContext& context) { return materialsRescan(context, *library); };
    };

    return {search, list, info, set, remove, groups, groupAddSpec, groupSetSpec, groupRemoveSpec, licenses, rescan};
}

}
