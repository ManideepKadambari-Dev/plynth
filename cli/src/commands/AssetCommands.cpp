#include "../CatalogCommon.h"
#include "../Common.h"

#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/AssetInfo.h>

#include <algorithm>
#include <memory>
#include <optional>

namespace plynth::cli {

namespace {

struct FilterOptions {
    std::string library;
    std::string text;
    std::string room;
    std::string category;
    std::vector<std::string> tags;
    std::string license;
    bool uncategorized = false;
    int limit = 0;
};

struct IdOptions {
    std::string library;
    std::string id;
};

struct RemoveAssetOptions {
    std::string library;
    std::string id;
    bool yes = false;
};

struct SetAssetOptions {
    std::string library;
    std::string id;
    std::optional<std::string> name;
    std::optional<std::string> categories;
    std::vector<std::string> addCategories;
    std::vector<std::string> removeCategories;
    std::optional<std::string> tags;
    std::vector<std::string> addTags;
    std::vector<std::string> removeTags;
    std::optional<std::string> license;
};

struct RoomAddOptions {
    std::string library;
    std::string id;
    std::string name;
};

struct RoomSetOptions {
    std::string library;
    std::string id;
    std::optional<std::string> name;
    std::optional<std::string> categories;
    std::optional<int> position;
};

struct CategoryOptions {
    std::string library;
    std::string id;
    std::string name;
};

// The unknown-id answer shared by the lookups below.
Answer notFound(const std::string& command, const std::string& what, const std::string& id) {
    return failure(command, fail(ErrorCode::NotFound, "There is no " + what + " " + id + "."));
}

// A query from the filter options. Fails with not_found when a room type, category or license is unknown.
bool buildQuery(const std::string& command, const OpenedCatalog& opened, const FilterOptions& options,
                AssetQuery& query, Answer& failed) {
    query.text = options.text;
    if (!options.room.empty()) {
        const auto rooms = opened.catalog->roomTypes();
        if (std::ranges::none_of(rooms, [&](const RoomTypeEntry& room) { return room.id == options.room; })) {
            failed = notFound(command, "room type", options.room);
            return false;
        }
        query.roomType = options.room;
    }
    if (!options.category.empty()) {
        const auto categories = opened.catalog->categories();
        if (std::ranges::none_of(categories, [&](const CategoryEntry& item) { return item.id == options.category; })) {
            failed = notFound(command, "category", options.category);
            return false;
        }
        query.category = options.category;
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
Answer assetsSearch(const std::string& command, const RunContext& context, const FilterOptions& options) {
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    AssetQuery query;
    if (!buildQuery(command, opened, options, query, failed)) {
        return failed;
    }
    Json assets = Json::array();
    for (const AssetRecord& record : opened.catalog->search(query)) {
        assets.push_back(assetEntry(record));
    }
    Json result = Json::object();
    const std::size_t count = assets.size();
    result["assets"] = std::move(assets);
    result["count"] = count;
    return success(command, std::move(result));
}

// One asset in full: its entry plus what its info.json says about the model.
Answer assetsInfo(const RunContext& context, const IdOptions& options) {
    const std::string command = "assets info";
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    const auto record = opened.catalog->asset(options.id);
    if (!record) {
        return notFound(command, "asset", options.id);
    }
    Json entry = assetEntry(*record);
    const AssetInfoRead read = readAssetInfo(record->folder / std::string(kAssetInfoFileName), record->id);
    if (!read.status.ok()) {
        return failure(command, read.status);
    }
    Json warnings = Json::array();
    for (const BlenderWarning& warning : read.info.warnings) {
        Json item = Json::object();
        item["code"] = warning.code;
        item["message"] = warning.message;
        warnings.push_back(std::move(item));
    }
    entry["objects"] = read.info.objects;
    entry["materials"] = read.info.materials;
    entry["textures"] = read.info.textures;
    entry["warnings"] = std::move(warnings);
    entry["blender_version"] = read.info.blenderVersion;
    return success(command, std::move(entry));
}

// The list after --categories or --tags, then the adds and removes, with repeats dropped.
std::vector<std::string> changedList(std::vector<std::string> current, const std::optional<std::string>& replace,
                                     const std::vector<std::string>& add, const std::vector<std::string>& remove) {
    if (replace) {
        current = splitList(*replace);
    }
    for (const std::string& item : add) {
        current.push_back(item);
    }
    std::erase_if(current, [&](const std::string& item) { return std::ranges::find(remove, item) != remove.end(); });
    std::vector<std::string> unique;
    for (const std::string& item : current) {
        if (std::ranges::find(unique, item) == unique.end()) {
            unique.push_back(item);
        }
    }
    return unique;
}

// Changes the name, categories, tags and license of an asset.
Answer assetsSet(const RunContext& context, const SetAssetOptions& options) {
    const std::string command = "assets set";
    const bool categoryChange = options.categories || !options.addCategories.empty() || !options.removeCategories.empty();
    const bool tagChange = options.tags || !options.addTags.empty() || !options.removeTags.empty();
    if (!options.name && !categoryChange && !tagChange && !options.license) {
        return badArguments(command, "bad_arguments", "Give at least one option to change. Run 'plynth help'.");
    }
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    const auto record = opened.catalog->asset(options.id);
    if (!record) {
        return notFound(command, "asset", options.id);
    }
    AssetEdit edit;
    edit.name = options.name;
    edit.license = options.license;
    if (categoryChange) {
        edit.categories =
            changedList(record->categories, options.categories, options.addCategories, options.removeCategories);
    }
    if (tagChange) {
        edit.tags = changedList(record->tags, options.tags, options.addTags, options.removeTags);
    }
    const Status status = opened.catalog->editAsset(options.id, edit);
    if (!status.ok()) {
        return failure(command, status);
    }
    return success(command, assetEntry(*opened.catalog->asset(options.id)));
}

// Deletes an asset's folder and index row. Needs --yes.
Answer assetsRemove(const RunContext& context, const RemoveAssetOptions& options) {
    const std::string command = "assets remove";
    if (!options.yes) {
        return badArguments(command, "bad_arguments",
                            "Removing an asset deletes its folder and cannot be undone. Add --yes to confirm.");
    }
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    const Status status = opened.catalog->removeAsset(options.id);
    if (!status.ok()) {
        return failure(command, status);
    }
    Json result = Json::object();
    result["removed"] = options.id;
    return success(command, std::move(result));
}

// Lists the room types of the library.
Answer assetsRooms(const RunContext& context, const std::string& library) {
    const std::string command = "assets rooms";
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, library, opened, failed)) {
        return failed;
    }
    Json rooms = Json::array();
    for (const RoomTypeEntry& entry : opened.catalog->roomTypes()) {
        rooms.push_back(roomTypeEntry(entry));
    }
    Json result = Json::object();
    result["room_types"] = std::move(rooms);
    return success(command, std::move(result));
}

// The entry of one room type, after a change.
Answer roomEntryAnswer(const std::string& command, const AssetCatalog& catalog, const std::string& id) {
    for (const RoomTypeEntry& entry : catalog.roomTypes()) {
        if (entry.id == id) {
            return success(command, roomTypeEntry(entry));
        }
    }
    return notFound(command, "room type", id);
}

// Adds a room type.
Answer roomAdd(const RunContext& context, const RoomAddOptions& options) {
    const std::string command = "assets room add";
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    const Status status = opened.catalog->addRoomType(options.id, options.name);
    if (!status.ok()) {
        return failure(command, status);
    }
    return roomEntryAnswer(command, *opened.catalog, options.id);
}

// Renames a room type, replaces its categories or moves it in the list.
Answer roomSet(const RunContext& context, const RoomSetOptions& options) {
    const std::string command = "assets room set";
    if (!options.name && !options.categories && !options.position) {
        return badArguments(command, "bad_arguments", "Give at least one option to change. Run 'plynth help'.");
    }
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    const std::vector<RoomTypeEntry> before = opened.catalog->roomTypes();
    if (std::ranges::none_of(before, [&](const RoomTypeEntry& entry) { return entry.id == options.id; })) {
        return notFound(command, "room type", options.id);
    }
    if (options.name) {
        const Status status = opened.catalog->renameRoomType(options.id, *options.name);
        if (!status.ok()) {
            return failure(command, status);
        }
    }
    if (options.categories) {
        const Status status = opened.catalog->setRoomTypeCategories(options.id, splitList(*options.categories));
        if (!status.ok()) {
            return failure(command, status);
        }
    }
    if (options.position) {
        const Status status = opened.catalog->moveRoomType(options.id, *options.position);
        if (!status.ok()) {
            return failure(command, status);
        }
    }
    return roomEntryAnswer(command, *opened.catalog, options.id);
}

// Removes a room type.
Answer roomRemove(const RunContext& context, const IdOptions& options) {
    const std::string command = "assets room remove";
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    const Status status = opened.catalog->removeRoomType(options.id);
    if (!status.ok()) {
        return failure(command, status);
    }
    Json result = Json::object();
    result["removed"] = options.id;
    return success(command, std::move(result));
}

// Lists the categories with how many assets hold each.
Answer assetsCategories(const RunContext& context, const std::string& library) {
    const std::string command = "assets categories";
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, library, opened, failed)) {
        return failed;
    }
    Json categories = Json::array();
    for (const CategoryEntry& entry : opened.catalog->categories()) {
        categories.push_back(categoryEntry(entry));
    }
    Json result = Json::object();
    result["categories"] = std::move(categories);
    return success(command, std::move(result));
}

// The entry of one category, after a change.
Answer categoryEntryAnswer(const std::string& command, const AssetCatalog& catalog, const std::string& id) {
    for (const CategoryEntry& entry : catalog.categories()) {
        if (entry.id == id) {
            return success(command, categoryEntry(entry));
        }
    }
    return notFound(command, "category", id);
}

// Adds a category.
Answer categoryAdd(const RunContext& context, const CategoryOptions& options) {
    const std::string command = "assets category add";
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    const Status status = opened.catalog->addCategory(options.id, options.name);
    if (!status.ok()) {
        return failure(command, status);
    }
    return categoryEntryAnswer(command, *opened.catalog, options.id);
}

// Renames a category.
Answer categorySet(const RunContext& context, const CategoryOptions& options) {
    const std::string command = "assets category set";
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    const Status status = opened.catalog->renameCategory(options.id, options.name);
    if (!status.ok()) {
        return failure(command, status);
    }
    return categoryEntryAnswer(command, *opened.catalog, options.id);
}

// Removes a category and says which assets lost it.
Answer categoryRemove(const RunContext& context, const IdOptions& options) {
    const std::string command = "assets category remove";
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    AssetQuery query;
    query.category = options.id;
    std::vector<std::string> changed;
    for (const AssetRecord& record : opened.catalog->search(query)) {
        changed.push_back(record.id);
    }
    const Status status = opened.catalog->removeCategory(options.id);
    if (!status.ok()) {
        return failure(command, status);
    }
    Json result = Json::object();
    result["removed"] = options.id;
    result["assets_changed"] = changed;
    return success(command, std::move(result));
}

// Lists the licenses.
Answer assetsLicenses(const RunContext& context, const std::string& library) {
    const std::string command = "assets licenses";
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, library, opened, failed)) {
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

// Adds the filter options shared by assets search and assets list.
void addFilterOptions(CLI::App& sub, FilterOptions& options) {
    addLibraryOption(sub, options.library);
    sub.add_option("--room", options.room, "Only assets in categories of this room type.")->type_name("id");
    sub.add_option("--category", options.category, "Only assets in this category.")->type_name("id");
    sub.add_option("--tag", options.tags, "Only assets with this tag. Repeat to require several.")
        ->type_name("tag");
    sub.add_option("--license", options.license, "Only assets with this license.")->type_name("id");
    sub.add_flag("--uncategorized", options.uncategorized, "Only assets without a category.");
    sub.add_option("--limit", options.limit, "Show at most this many assets.")->type_name("number");
}

// Adds the library option and the id argument shared by the id commands.
void addIdOptions(CLI::App& sub, IdOptions& options, const std::string& what) {
    addLibraryOption(sub, options.library);
    sub.add_option("id", options.id, "Id of the " + what + ".")->type_name("id")->required();
}

}

std::vector<CommandSpec> assetCommands() {
    CommandSpec search;
    search.words = {"assets", "search"};
    search.description = "Search the assets by words and filters.";
    search.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<FilterOptions>();
        sub.add_option("text", options->text, "Words to find at the start of a name, id, tag or category.")
            ->type_name("text");
        addFilterOptions(sub, *options);
        return [options](const RunContext& context) { return assetsSearch("assets search", context, *options); };
    };

    CommandSpec list;
    list.words = {"assets", "list"};
    list.description = "List the assets, with the same filters as search.";
    list.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<FilterOptions>();
        addFilterOptions(sub, *options);
        return [options](const RunContext& context) { return assetsSearch("assets list", context, *options); };
    };

    CommandSpec info;
    info.words = {"assets", "info"};
    info.description = "Show one asset with its objects, materials, textures and warnings.";
    info.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<IdOptions>();
        addIdOptions(sub, *options, "asset");
        return [options](const RunContext& context) { return assetsInfo(context, *options); };
    };

    CommandSpec set;
    set.words = {"assets", "set"};
    set.description = "Change an asset's name, categories, tags or license.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SetAssetOptions>();
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the asset.")->type_name("id")->required();
        sub.add_option("--name", options->name, "New name.")->type_name("text");
        sub.add_option("--categories", options->categories, "Replace the categories. Empty clears them.")
            ->type_name("a,b");
        sub.add_option("--add-category", options->addCategories, "Add a category. Repeatable.")->type_name("id");
        sub.add_option("--remove-category", options->removeCategories, "Remove a category. Repeatable.")
            ->type_name("id");
        sub.add_option("--tags", options->tags, "Replace the tags. Empty clears them.")->type_name("a,b");
        sub.add_option("--add-tag", options->addTags, "Add a tag. Repeatable.")->type_name("tag");
        sub.add_option("--remove-tag", options->removeTags, "Remove a tag. Repeatable.")->type_name("tag");
        sub.add_option("--license", options->license, "License id.")->type_name("id");
        return [options](const RunContext& context) { return assetsSet(context, *options); };
    };

    CommandSpec remove;
    remove.words = {"assets", "remove"};
    remove.description = "Delete an asset and its folder. Needs --yes.";
    remove.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RemoveAssetOptions>();
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the asset.")->type_name("id")->required();
        sub.add_flag("--yes", options->yes, "Confirm the deletion.");
        return [options](const RunContext& context) { return assetsRemove(context, *options); };
    };

    CommandSpec rooms;
    rooms.words = {"assets", "rooms"};
    rooms.description = "List the room types of the library with their categories.";
    rooms.setup = [](CLI::App& sub) -> Handler {
        auto library = std::make_shared<std::string>();
        addLibraryOption(sub, *library);
        return [library](const RunContext& context) { return assetsRooms(context, *library); };
    };

    CommandSpec roomAddSpec;
    roomAddSpec.words = {"assets", "room", "add"};
    roomAddSpec.description = "Add a room type.";
    roomAddSpec.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RoomAddOptions>();
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the new room type.")->type_name("id")->required();
        sub.add_option("--name", options->name, "Name shown to the user.")->type_name("text")->required();
        return [options](const RunContext& context) { return roomAdd(context, *options); };
    };

    CommandSpec roomSetSpec;
    roomSetSpec.words = {"assets", "room", "set"};
    roomSetSpec.description = "Rename a room type, replace its categories or move it in the list.";
    roomSetSpec.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RoomSetOptions>();
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the room type.")->type_name("id")->required();
        sub.add_option("--name", options->name, "New name.")->type_name("text");
        sub.add_option("--categories", options->categories, "Replace the categories.")->type_name("a,b");
        sub.add_option("--position", options->position, "Place in the list, from 0.")->type_name("number");
        return [options](const RunContext& context) { return roomSet(context, *options); };
    };

    CommandSpec roomRemoveSpec;
    roomRemoveSpec.words = {"assets", "room", "remove"};
    roomRemoveSpec.description = "Remove a room type.";
    roomRemoveSpec.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<IdOptions>();
        addIdOptions(sub, *options, "room type");
        return [options](const RunContext& context) { return roomRemove(context, *options); };
    };

    CommandSpec categories;
    categories.words = {"assets", "categories"};
    categories.description = "List the categories with how many assets hold each.";
    categories.setup = [](CLI::App& sub) -> Handler {
        auto library = std::make_shared<std::string>();
        addLibraryOption(sub, *library);
        return [library](const RunContext& context) { return assetsCategories(context, *library); };
    };

    CommandSpec categoryAddSpec;
    categoryAddSpec.words = {"assets", "category", "add"};
    categoryAddSpec.description = "Add a category.";
    categoryAddSpec.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<CategoryOptions>();
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the new category.")->type_name("id")->required();
        sub.add_option("--name", options->name, "Name shown to the user.")->type_name("text")->required();
        return [options](const RunContext& context) { return categoryAdd(context, *options); };
    };

    CommandSpec categorySetSpec;
    categorySetSpec.words = {"assets", "category", "set"};
    categorySetSpec.description = "Rename a category.";
    categorySetSpec.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<CategoryOptions>();
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the category.")->type_name("id")->required();
        sub.add_option("--name", options->name, "New name.")->type_name("text")->required();
        return [options](const RunContext& context) { return categorySet(context, *options); };
    };

    CommandSpec categoryRemoveSpec;
    categoryRemoveSpec.words = {"assets", "category", "remove"};
    categoryRemoveSpec.description = "Remove a category from the list and from every asset that has it.";
    categoryRemoveSpec.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<IdOptions>();
        addIdOptions(sub, *options, "category");
        return [options](const RunContext& context) { return categoryRemove(context, *options); };
    };

    CommandSpec licenses;
    licenses.words = {"assets", "licenses"};
    licenses.description = "List the licenses an asset can have.";
    licenses.setup = [](CLI::App& sub) -> Handler {
        auto library = std::make_shared<std::string>();
        addLibraryOption(sub, *library);
        return [library](const RunContext& context) { return assetsLicenses(context, *library); };
    };

    return {search,         list,           info,           set,           remove,
            rooms,          roomAddSpec,    roomSetSpec,    roomRemoveSpec, categories,
            categoryAddSpec, categorySetSpec, categoryRemoveSpec, licenses};
}

}
