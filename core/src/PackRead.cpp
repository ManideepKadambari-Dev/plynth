#include "PackSupport.h"

#include "plynth/core/OpeningShapes.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/catalog/AssetCatalog.h"
#include "plynth/core/catalog/Importer.h"
#include "plynth/core/catalog/MaterialImporter.h"
#include "plynth/core/catalog/MaterialCatalog.h"

#include <fstream>
#include <memory>
#include <set>
#include <utility>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// A refusal of the pack's content.
Status badPack(const std::string& message)
{
    return fail(ErrorCode::BadFile, "The pack cannot be used: " + message);
}

// The folder in the library where an entry's item lives. Shapes and trim profiles live in shapes/.
fs::path libraryFolder(const LibraryPaths& library, const PackEntry& entry)
{
    switch (entry.kind) {
    case PackKind::Asset: return library.assetItems / pathFromUtf8(entry.id);
    case PackKind::Material: return library.materialItems / pathFromUtf8(entry.id);
    case PackKind::Shape:
    case PackKind::TrimProfile: return library.shapes;
    case PackKind::Generator: break;
    }
    return library.generators;
}

// The entry's file paths with the pack prefix taken off.
std::vector<std::string> withoutPrefix(const PackEntry& entry)
{
    const std::string prefix = pack::packPrefix(entry.kind, entry.id);
    std::vector<std::string> files;
    for (const std::string& file : entry.files) files.push_back(file.substr(prefix.size()));
    return files;
}

// Checks every file under pack/: no links, nothing but pack.json and the listed files.
Status checkPackFolder(const fs::path& packFolder, const PackManifest& manifest)
{
    std::set<std::string> listed;
    for (const PackEntry& entry : manifest.entries) listed.insert(entry.files.begin(), entry.files.end());
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(packFolder, fs::directory_options::none, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path& path = it->path();
        const std::string relative = pathToUtf8(path.lexically_relative(packFolder));
        if (pack::isLink(path)) {
            it.disable_recursion_pending();
            return badPack(relative + " is a link. Packs never use links.");
        }
        if (it->is_directory(ec)) continue;
        if (!it->is_regular_file(ec)) return badPack(relative + " is not a plain file.");
        if (relative == kPackFileName) continue;
        if (!listed.count(relative)) return badPack(relative + " is not listed in pack.json.");
    }
    if (ec) return fail(ErrorCode::CannotRead, "The pack folder could not be read.");
    for (const std::string& file : listed) {
        if (!fs::is_regular_file(packFolder / pathFromUtf8(file), ec)) return badPack(file + " is listed but missing.");
    }
    return {};
}

// Says whether the item in the library holds the same files as the pack, info.json and thumb.png left aside.
bool sameItem(const fs::path& packItem, const fs::path& libraryItem, const std::vector<std::string>& files)
{
    for (const std::string& file : files) {
        if (file == "info.json" || file == "thumb.png") continue;
        if (!pack::sameBytes(packItem / pathFromUtf8(file), libraryItem / pathFromUtf8(file))) return false;
    }
    return true;
}

// Checks a copied asset or material against its own info.json, so a pack cannot pass a store item off as copied.
template <typename Read, typename List>
Status checkCopied(const PackEntry& entry, const fs::path& packItem, Read read, List list)
{
    const auto info = read(packItem / "info.json", entry.id);
    if (!info.status.ok()) return badPack("the info.json of " + entry.id + " is refused: " + info.status.message);
    if (info.info.id != entry.id || info.info.source.kind != entry.source.kind ||
        info.info.source.reference != entry.source.reference || info.info.license != entry.license)
        return badPack("the info.json of " + entry.id + " does not match pack.json.");
    if (packCopyRule(entry.kind, info.info.source, info.info.license) != PackReason::None)
        return badPack(entry.id + " may only be named in a pack, but its files are there.");
    std::string why;
    const std::vector<std::string> files = list(packItem, info.info, &why);
    if (files.empty()) return badPack(why);
    if (files != withoutPrefix(entry)) return badPack("the files of " + entry.id + " do not match its info.json.");
    return {};
}

// The command that gets a referenced item on this machine. A store address is imported as it is; a file such as a
// Fab zip has to be downloaded again first, and the command names the importer its source kind came from.
std::string importCommand(const PackEntry& entry)
{
    const bool material = entry.kind == PackKind::Material;
    const std::string group = material ? "materials" : "assets";
    std::string command = "plynth " + group + " import \"" + entry.source.reference + "\"";
    const std::string& kind = entry.source.kind;
    const bool address = entry.source.reference.rfind("http://", 0) == 0 || entry.source.reference.rfind("https://", 0) == 0;
    const bool importerNamed = material ? findMaterialImporter(kind) != nullptr : findImporter(kind) != nullptr;
    if (!address && importerNamed) command += " --importer " + kind;
    return command;
}

// Removes what an unpack added, ignoring errors, so a failure leaves the library as it was.
struct Rollback {
    std::vector<fs::path> paths;
    std::vector<std::string> indexedAssets;
    std::vector<std::string> indexedMaterials;
    std::vector<std::string> roomTypes;
    AssetCatalog* assets = nullptr;
    MaterialCatalog* materials = nullptr;

    void run()
    {
        for (const std::string& id : roomTypes) assets->removeRoomType(id);
        for (const std::string& id : indexedAssets) assets->removeAsset(id);
        for (const std::string& id : indexedMaterials) materials->removeMaterial(id);
        std::error_code ec;
        for (const fs::path& path : paths) fs::remove_all(path, ec);
    }
};

}

// Works out what unpack would do with each entry, after checking the whole pack.
UnpackPlan planUnpack(const fs::path& projectFolder, const LibraryPaths& library)
{
    UnpackPlan plan;
    const PackRead read = readPack(projectFolder);
    if (!read.status.ok()) {
        plan.status = read.status;
        return plan;
    }
    if (!isLibraryFolder(library.root)) {
        plan.status = fail(ErrorCode::NoLibrary, "No library is set to unpack into. Run plynth library init.");
        return plan;
    }
    const fs::path packFolder = projectFolder / pathFromUtf8(kPackFolderName);
    if (Status checked = checkPackFolder(packFolder, read.manifest); !checked.ok()) {
        plan.status = checked;
        return plan;
    }

    std::error_code ec;
    for (const PackEntry& entry : read.manifest.entries) {
        UnpackEntry out;
        out.entry = entry;
        const fs::path target = libraryFolder(library, entry);
        const fs::path packItem = packFolder / pathFromUtf8(pack::packPrefix(entry.kind, entry.id));
        if (entry.state == PackState::Missing) {
            out.action = UnpackAction::Missing;
        } else if (entry.kind == PackKind::Generator) {
            const bool there = fs::is_regular_file(library.generators / pathFromUtf8(entry.id + ".py"), ec);
            out.action = there ? UnpackAction::Present : UnpackAction::Missing;
        } else if (entry.state == PackState::Reference) {
            AssetSource present;
            bool readable = false;
            if (entry.kind == PackKind::Asset) {
                const AssetInfoRead info = readAssetInfo(target / "info.json", entry.id);
                readable = info.status.ok();
                present = info.info.source;
            } else if (entry.kind == PackKind::Material) {
                const MaterialInfoRead info = readMaterialInfo(target / "info.json", entry.id);
                readable = info.status.ok();
                present = info.info.source;
            }
            if (!fs::exists(target, ec)) {
                out.action = UnpackAction::NeedsDownload;
                out.command = importCommand(entry);
            } else if (readable && present.kind == entry.source.kind && present.reference == entry.source.reference) {
                out.action = UnpackAction::Present;
            } else {
                out.action = UnpackAction::Conflict;
            }
        } else if (entry.kind == PackKind::Shape || entry.kind == PackKind::TrimProfile) {
            const std::string name = pack::shapeFileName(entry.kind, entry.id);
            const fs::path packed = packFolder / pathFromUtf8("shapes/" + name);
            if (entry.kind == PackKind::Shape) {
                const ShapeFile shape = readShapeFile(packed);
                if (!shape.status.ok() || shape.id != entry.id) {
                    plan.status = badPack("the shape " + entry.id + " is refused: " + shape.status.message);
                    return plan;
                }
            } else {
                std::ifstream in(packed, std::ios::binary);
                if (Json::parse(in, nullptr, false).is_discarded()) {
                    plan.status = badPack("the trim profile " + entry.id + " is not valid JSON.");
                    return plan;
                }
            }
            const fs::path existing = library.shapes / pathFromUtf8(name);
            if (!fs::exists(existing, ec)) out.action = UnpackAction::Add;
            else out.action = pack::sameBytes(packed, existing) ? UnpackAction::Present : UnpackAction::Conflict;
        } else {
            const Status checked =
                entry.kind == PackKind::Asset
                    ? checkCopied(entry, packItem, [](const fs::path& f, const std::string& id) { return readAssetInfo(f, id); }, pack::assetFiles)
                    : checkCopied(entry, packItem, [](const fs::path& f, const std::string& id) { return readMaterialInfo(f, id); }, pack::materialFiles);
            if (!checked.ok()) {
                plan.status = checked;
                return plan;
            }
            if (!fs::exists(target, ec)) out.action = UnpackAction::Add;
            else out.action = sameItem(packItem, target, withoutPrefix(entry)) ? UnpackAction::Present : UnpackAction::Conflict;
        }
        plan.entries.push_back(std::move(out));
    }

    if (!read.manifest.roomTypes.empty()) {
        Status status;
        auto catalog = AssetCatalog::open(library, status);
        if (!catalog) {
            plan.status = status.ok() ? fail(ErrorCode::DatabaseError, "The assets catalog could not be opened.") : status;
            return plan;
        }
        const std::vector<RoomType> known = catalog->roomTypeList();
        for (const RoomType& room : read.manifest.roomTypes) {
            bool have = false;
            for (const RoomType& k : known) have = have || k.id == room.id;
            if (!have) plan.roomTypesToAdd.push_back(room);
        }
    }
    return plan;
}

// Copies the new items into the library through staging folders, then indexes them, undoing it all on a failure.
UnpackReport unpackProject(const fs::path& projectFolder, const LibraryPaths& library, const UnpackOptions& options,
                           std::stop_token stop)
{
    UnpackReport report;
    const UnpackPlan plan = planUnpack(projectFolder, library);
    report.entries = plan.entries;
    if (!plan.status.ok()) {
        report.status = plan.status;
        return report;
    }
    report.complete = true;
    std::string conflicts;
    for (const UnpackEntry& entry : plan.entries) {
        if (entry.action == UnpackAction::Conflict) conflicts += (conflicts.empty() ? "" : ", ") + entry.entry.id;
        if (entry.action == UnpackAction::Missing || entry.action == UnpackAction::NeedsDownload) report.complete = false;
    }
    if (!conflicts.empty() && !options.keepLibrary) {
        report.complete = false;
        report.status = fail(ErrorCode::AlreadyExists, "The library already has different items with these ids: " + conflicts +
                                                           ". Nothing was unpacked. Unpack with --keep-library to keep the library's items.");
        return report;
    }

    Status opened;
    auto assets = AssetCatalog::open(library, opened);
    if (!assets) {
        report.status = opened.ok() ? fail(ErrorCode::DatabaseError, "The assets catalog could not be opened.") : opened;
        return report;
    }
    auto materials = MaterialCatalog::open(library, opened);
    if (!materials) {
        report.status = opened.ok() ? fail(ErrorCode::DatabaseError, "The materials catalog could not be opened.") : opened;
        return report;
    }

    const fs::path packFolder = projectFolder / pathFromUtf8(kPackFolderName);
    const std::string suffix = pack::randomSuffix();
    const fs::path assetStage = library.assetStaging / pathFromUtf8("unpack-" + suffix);
    const fs::path materialStage = library.materialStaging / pathFromUtf8("unpack-" + suffix);
    const auto removeStages = [&]() {
        std::error_code ec;
        fs::remove_all(assetStage, ec);
        fs::remove_all(materialStage, ec);
    };
    Rollback rollback;
    rollback.assets = assets.get();
    rollback.materials = materials.get();
    const auto failWith = [&](Status status) {
        rollback.run();
        removeStages();
        report.status = std::move(status);
        report.added = 0;
        report.complete = false;
        return report;
    };

    // First every new file goes into staging folders inside the library, so a failure here touches nothing.
    std::vector<const UnpackEntry*> adds;
    for (const UnpackEntry& entry : plan.entries) {
        if (entry.action == UnpackAction::Add) adds.push_back(&entry);
    }
    for (const UnpackEntry* add : adds) {
        const PackEntry& e = add->entry;
        const fs::path stage = e.kind == PackKind::Material ? materialStage : assetStage;
        for (const std::string& file : e.files) {
            if (stop.stop_requested()) return failWith(fail(ErrorCode::Cancelled, "Unpacking was stopped. Nothing was added."));
            if (!pack::copyKeepingTime(packFolder / pathFromUtf8(file), stage / pathFromUtf8(file)))
                return failWith(fail(ErrorCode::CannotWrite, file + " could not be copied into the library."));
        }
    }
    if (stop.stop_requested()) return failWith(fail(ErrorCode::Cancelled, "Unpacking was stopped. Nothing was added."));

    // Then each item is moved into place and indexed.
    std::error_code ec;
    for (const UnpackEntry* add : adds) {
        const PackEntry& e = add->entry;
        if (e.kind == PackKind::Shape || e.kind == PackKind::TrimProfile) {
            const std::string name = pack::shapeFileName(e.kind, e.id);
            fs::create_directories(library.shapes, ec);
            const fs::path to = library.shapes / pathFromUtf8(name);
            fs::rename(assetStage / pathFromUtf8("shapes/" + name), to, ec);
            if (ec) return failWith(fail(ErrorCode::CannotWrite, "The shape " + e.id + " could not be put in the library."));
            rollback.paths.push_back(to);
            continue;
        }
        const bool isAsset = e.kind == PackKind::Asset;
        const fs::path from = (isAsset ? assetStage / "assets" : materialStage / "materials") / pathFromUtf8(e.id);
        const fs::path to = (isAsset ? library.assetItems : library.materialItems) / pathFromUtf8(e.id);
        fs::create_directories(to.parent_path(), ec);
        fs::rename(from, to, ec);
        if (ec) return failWith(fail(ErrorCode::CannotWrite, "The " + std::string(packKindName(e.kind)) + " " + e.id + " could not be put in the library."));
        rollback.paths.push_back(to);
        if (isAsset) {
            const AssetInfoRead info = readAssetInfo(to / "info.json", e.id);
            const Status indexed = info.status.ok() ? assets->indexAsset(info.info) : info.status;
            if (!indexed.ok()) return failWith(indexed);
            rollback.indexedAssets.push_back(e.id);
        } else {
            const MaterialInfoRead info = readMaterialInfo(to / "info.json", e.id);
            const Status indexed = info.status.ok() ? materials->indexMaterial(info.info) : info.status;
            if (!indexed.ok()) return failWith(indexed);
            rollback.indexedMaterials.push_back(e.id);
        }
    }
    for (const RoomType& room : plan.roomTypesToAdd) {
        if (Status added = assets->addRoomType(room.id, room.name); !added.ok()) return failWith(added);
        rollback.roomTypes.push_back(room.id);
    }
    removeStages();
    report.added = static_cast<int>(adds.size());
    return report;
}

}
