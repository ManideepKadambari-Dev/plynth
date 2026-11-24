#include "PackSupport.h"

#include "FileIo.h"

#include "plynth/core/OpeningShapes.h"
#include "plynth/core/ProjectFiles.h"

#include <set>
#include <utility>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// One file to copy into the pack.
struct Copy {
    fs::path from;
    std::string to; // relative to pack/
};

// An entry for an item the project uses but the library cannot give.
PackEntry missingEntry(PackKind kind, const std::string& id, PackReason reason)
{
    PackEntry entry;
    entry.kind = kind;
    entry.id = id;
    entry.name = id;
    entry.state = PackState::Missing;
    entry.reason = reason;
    return entry;
}

// Works out the entry of one asset or material and the files to copy for it.
template <typename Info, typename Read, typename List>
PackEntry catalogEntry(PackKind kind, const std::string& id, const fs::path& items, Read read, List list,
                       std::vector<Copy>& copies)
{
    const fs::path folder = items / pathFromUtf8(id);
    std::error_code ec;
    if (pack::isLink(folder) || !fs::is_directory(folder, ec)) return missingEntry(kind, id, PackReason::NotInLibrary);
    const auto infoRead = read(folder / "info.json", id);
    if (!infoRead.status.ok()) return missingEntry(kind, id, PackReason::Broken);
    const Info& info = infoRead.info;

    PackEntry entry;
    entry.kind = kind;
    entry.id = id;
    entry.name = info.name;
    entry.source = info.source;
    entry.license = info.license;
    entry.reason = packCopyRule(kind, info.source, info.license);
    if (entry.reason != PackReason::None) {
        entry.state = PackState::Reference;
        return entry;
    }
    std::string why;
    const std::vector<std::string> files = list(folder, info, &why);
    if (files.empty()) {
        entry.state = PackState::Missing;
        entry.reason = PackReason::Broken;
        return entry;
    }
    entry.state = PackState::Copied;
    const std::string prefix = pack::packPrefix(kind, id);
    for (const std::string& file : files) {
        entry.files.push_back(prefix + file);
        copies.push_back({folder / pathFromUtf8(file), prefix + file});
    }
    return entry;
}

// Works out the entry of a shape or trim profile file in the library's shapes/ folder.
PackEntry shapeEntry(PackKind kind, const std::string& id, const LibraryPaths& library, std::vector<Copy>& copies)
{
    const std::string name = pack::shapeFileName(kind, id);
    const fs::path file = library.shapes / pathFromUtf8(name);
    std::error_code ec;
    if (pack::isLink(file) || !fs::is_regular_file(file, ec)) return missingEntry(kind, id, PackReason::NotInLibrary);
    PackEntry entry;
    entry.kind = kind;
    entry.id = id;
    entry.name = id;
    if (kind == PackKind::Shape) {
        const ShapeFile shape = readShapeFile(file);
        if (!shape.status.ok() || shape.id != id) return missingEntry(kind, id, PackReason::Broken);
        entry.name = shape.name;
    }
    entry.state = PackState::Copied;
    entry.files.push_back("shapes/" + name);
    copies.push_back({file, "shapes/" + name});
    return entry;
}

// Removes a folder and everything in it, ignoring errors.
void removeTree(const fs::path& folder)
{
    std::error_code ec;
    fs::remove_all(folder, ec);
}

}

// Collects the entries, copies the files into a staging folder and swaps it in for the old pack.
PackReport packProject(const PackRequest& request, std::stop_token stop)
{
    PackReport report;
    report.checkedOnly = request.checkOnly;
    std::error_code ec;
    const fs::path folder = fs::absolute(request.projectFolder, ec).lexically_normal();
    report.folder = folder / pathFromUtf8(kPackFolderName);
    if (!isProjectFolder(folder)) {
        report.status = fail(ErrorCode::NotAProject, "The folder " + pathToUtf8(folder) + " holds no project.");
        return report;
    }
    const AppStateRead app = readAppState(folder);
    if (app.open && app.state.modified) {
        report.status = fail(ErrorCode::UnsavedChanges, "The app has unsaved changes in this project. Save them, then pack.");
        return report;
    }
    if (!request.contents.used.empty() && !isLibraryFolder(request.library.root)) {
        report.status = fail(ErrorCode::NoLibrary, "No library is set, so the items the project uses cannot be packed. Run plynth library init.");
        return report;
    }

    const ProjectLoad load = loadProject(folder);
    report.manifest.project = load.status.ok() ? load.project.settings.name : pathToUtf8(folder.filename());
    report.manifest.created = sessionNow();
    std::set<std::string> roomIds;
    for (const RoomType& room : request.contents.roomTypes) {
        if (roomIds.insert(room.id).second) report.manifest.roomTypes.push_back(room);
    }

    std::vector<Copy> copies;
    std::set<std::pair<int, std::string>> seen;
    for (const PackUse& use : request.contents.used) {
        if (!seen.insert({static_cast<int>(use.kind), use.id}).second) continue;
        PackEntry entry;
        if (!checkCatalogId(use.id).ok()) {
            entry = missingEntry(use.kind, use.id, PackReason::NotInLibrary);
        } else if (use.kind == PackKind::Asset) {
            entry = catalogEntry<AssetInfo>(use.kind, use.id, request.library.assetItems,
                                            [](const fs::path& f, const std::string& id) { return readAssetInfo(f, id); },
                                            pack::assetFiles, copies);
        } else if (use.kind == PackKind::Material) {
            entry = catalogEntry<MaterialInfo>(use.kind, use.id, request.library.materialItems,
                                               [](const fs::path& f, const std::string& id) { return readMaterialInfo(f, id); },
                                               pack::materialFiles, copies);
        } else if (use.kind == PackKind::Shape || use.kind == PackKind::TrimProfile) {
            entry = shapeEntry(use.kind, use.id, request.library, copies);
        } else {
            const fs::path script = request.library.generators / pathFromUtf8(use.id + ".py");
            if (!fs::is_regular_file(script, ec)) {
                entry = missingEntry(use.kind, use.id, PackReason::NotInLibrary);
            } else {
                entry.kind = use.kind;
                entry.id = use.id;
                entry.name = use.id;
                entry.state = PackState::Reference;
                entry.reason = PackReason::Script;
            }
        }
        if (entry.state == PackState::Copied && (entry.kind == PackKind::Asset || entry.kind == PackKind::Material) &&
            entry.license == kUnknownLicenseId) {
            report.problems.push_back({"license_unknown", BuildSeverity::Warning,
                                       "The " + std::string(packKindName(entry.kind)) + " " + entry.id +
                                           " has no known license. It was copied; check that it may be shared.",
                                       {}, entry.id, "pack"});
        }
        if (entry.state == PackState::Missing) {
            report.problems.push_back({"item_missing", BuildSeverity::Warning,
                                       "The " + std::string(packKindName(entry.kind)) + " " + entry.id +
                                           (entry.reason == PackReason::Broken ? " is broken in the library" : " is not in the library") +
                                           ", so it is not in the pack.",
                                       {}, entry.id, "pack"});
        }
        report.manifest.entries.push_back(std::move(entry));
    }
    report.complete = true;
    for (const PackEntry& entry : report.manifest.entries) report.complete = report.complete && entry.state != PackState::Missing;
    for (const Copy& copy : copies) {
        const auto size = fs::file_size(copy.from, ec);
        if (!ec) report.bytes += static_cast<long long>(size);
    }
    if (request.checkOnly) return report;

    const std::string suffix = pack::randomSuffix();
    const fs::path staging = folder / pathFromUtf8(".pack-staging-" + suffix);
    const fs::path old = folder / pathFromUtf8(".pack-old-" + suffix);
    const auto failWith = [&](Status status) {
        removeTree(staging);
        report.status = std::move(status);
        return report;
    };
    fs::create_directories(staging, ec);
    if (ec) return failWith(fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(staging) + " could not be created."));
    for (const Copy& copy : copies) {
        if (stop.stop_requested()) return failWith(fail(ErrorCode::Cancelled, "Packing was stopped. The old pack is kept."));
        if (!pack::copyKeepingTime(copy.from, staging / pathFromUtf8(copy.to)))
            return failWith(fail(ErrorCode::CannotWrite, pathToUtf8(copy.from) + " could not be copied into the pack."));
    }
    if (stop.stop_requested()) return failWith(fail(ErrorCode::Cancelled, "Packing was stopped. The old pack is kept."));
    const std::string text = packManifestToJson(report.manifest).dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
    if (Status written = fileio::writeAtomic(staging / pathFromUtf8(kPackFileName), text); !written.ok()) return failWith(written);

    const fs::path target = report.folder;
    const bool hadOld = fs::exists(target, ec);
    if (hadOld) {
        fs::rename(target, old, ec);
        if (ec) return failWith(fail(ErrorCode::CannotWrite, "The old pack could not be moved aside. Is a file in it open?"));
    }
    fs::rename(staging, target, ec);
    if (ec) {
        if (hadOld) {
            std::error_code back;
            fs::rename(old, target, back);
        }
        return failWith(fail(ErrorCode::CannotWrite, "The new pack could not be put in place."));
    }
    if (hadOld) removeTree(old);
    return report;
}

}
