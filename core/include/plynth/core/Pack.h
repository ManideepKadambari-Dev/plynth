#pragma once

#include "BuildProblem.h"
#include "EditSession.h"
#include "Library.h"
#include "RoomTypes.h"
#include "Status.h"
#include "catalog/AssetInfo.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// Moving a project to another machine (DESIGN.md section 5, rule 3). "pack" copies the library items a project
// uses into the project's pack/ folder, "unpack" copies them from there into another library, so the project
// keeps pointing to its items by id. pack/ is a transport folder: builds and catalogs never read it.
//
//   pack/
//     pack.json         the manifest, schema/pack.schema.json
//     assets/<id>/      info.json, asset.blend, asset.fbx, thumb.png and the textures and maps info.json lists
//     materials/<id>/   info.json, thumb.png and the maps info.json lists
//     shapes/           <id>.shape.json, <id>.profile.json
//
// Only a listed set of files is copied, never a whole folder. Store items, items whose origin is not known,
// items whose license forbids sharing and generator scripts are only named (a reference), with their id, name,
// source and license, so the other machine can get them itself. The manifest holds no absolute path.
// A pack is untrusted input: unpack checks everything before it writes anything, and runs nothing from it.

inline constexpr std::string_view kPackFolderName = "pack";
inline constexpr std::string_view kPackFileName = "pack.json";
inline constexpr std::string_view kPackFormat = "plynth-pack";
inline constexpr int kPackVersion = 1;

enum class PackKind {
    Asset,       // "asset"
    Material,    // "material"
    Shape,       // "shape": a custom opening outline, shapes/<id>.shape.json
    TrimProfile, // "trim_profile": a trim profile, shapes/<id>.profile.json
    Generator,   // "generator": a generator script, generators/<id>.py, never copied
};

enum class PackState {
    Copied,    // "copied": its files are in the pack
    Reference, // "reference": only named
    Missing,   // "missing": the project uses it but the library does not have it, or it is broken
};

enum class PackReason {
    None,          // "none": copied
    Store,         // "store": it came from a store
    License,       // "license": its license does not allow sharing
    UnknownOrigin, // "unknown_origin": where it came from is not known
    Script,        // "script": a generator script, which is never copied
    NotInLibrary,  // "not_in_library"
    Broken,        // "broken": its files are missing or its info.json cannot be read
};

std::string_view packKindName(PackKind kind);
std::optional<PackKind> packKindFromName(std::string_view name);
std::string_view packStateName(PackState state);
std::optional<PackState> packStateFromName(std::string_view name);
std::string_view packReasonName(PackReason reason);
std::optional<PackReason> packReasonFromName(std::string_view name);

struct PackEntry {
    PackKind kind = PackKind::Asset;
    std::string id;
    std::string name;
    PackState state = PackState::Copied;
    PackReason reason = PackReason::None;
    AssetSource source;             // empty for shapes, trim profiles and generators
    std::string license;            // empty for shapes, trim profiles and generators
    std::vector<std::string> files; // relative to pack/, forward slashes; empty unless copied

    bool operator==(const PackEntry&) const = default;
};

struct PackManifest {
    std::string project; // the project's name
    SessionTime created{};
    std::vector<PackEntry> entries;
    std::vector<RoomType> roomTypes; // the custom room types the project uses

    bool operator==(const PackManifest&) const = default;
};

// What a project uses. Worked out from the project by the caller.
struct PackUse {
    PackKind kind = PackKind::Asset;
    std::string id;
};

struct PackContents {
    std::vector<PackUse> used;
    std::vector<RoomType> roomTypes;
};

// None when the item may be copied, otherwise why it becomes a reference. Generators are always Script. Assets
// and materials from a store are Store; ones with a source kind other than local_file, texture_set and
// blend_material, or taken from a bare asset.blend, are UnknownOrigin; ones with the license royalty_free or
// personal_use are License. Shapes and trim profiles are always copied.
PackReason packCopyRule(PackKind kind, const AssetSource& source, std::string_view license);

struct PackRequest {
    std::filesystem::path projectFolder;
    PackContents contents;
    LibraryPaths library;
    bool checkOnly = false; // work out the answer, write nothing
};

struct PackReport {
    // NotAProject, NoLibrary, UnsavedChanges while the app has unsaved changes, CannotRead, CannotWrite,
    // Cancelled. Ok when the pack was written, even with missing items.
    Status status;
    std::filesystem::path folder; // the pack folder, absolute
    PackManifest manifest;
    long long bytes = 0;          // of the copied files
    bool complete = false;        // no entry is missing
    bool checkedOnly = false;
    std::vector<BuildProblem> problems; // warnings: license_unknown, item_missing
};

// Writes pack/ for a project. The pack is made in a staging folder inside the project and then swapped in, so a
// failure or a cancel leaves the old pack as it was. Copies keep their write times, so a rescan on the other
// machine renders nothing again.
PackReport packProject(const PackRequest& request, std::stop_token stop = {});

struct PackRead {
    // NotFound without pack/pack.json, BadFile when it breaks the rules, UnsupportedVersion when newer.
    Status status;
    PackManifest manifest;
};

// Reads and checks pack/pack.json: the format, the names, every file path (relative, inside its entry's folder,
// no "..", no drive, no reserved name, a known file type) and that no copied entry is one the copy rule says
// must be a reference.
PackRead readPack(const std::filesystem::path& projectFolder);

enum class UnpackAction {
    Add,           // "add": copied into the library
    Present,       // "present": the library already has the same item
    Conflict,      // "conflict": the library has a different item with this id
    NeedsDownload, // "needs_download": a reference the library lacks; get it with the command given
    Missing,       // "missing": the pack does not have it either
};

std::string_view unpackActionName(UnpackAction action);

struct UnpackEntry {
    PackEntry entry;
    UnpackAction action = UnpackAction::Add;
    std::string command; // for NeedsDownload: the plynth command that gets it, empty otherwise

    bool operator==(const UnpackEntry&) const = default;
};

struct UnpackPlan {
    // The errors of readPack, and BadFile for a file that is missing, unlisted, a link, or an info.json that
    // its reader refuses or that disagrees with the manifest. NoLibrary when the library is not one.
    Status status;
    std::vector<UnpackEntry> entries;
    std::vector<RoomType> roomTypesToAdd;
};

// Says what unpack would do, writing nothing. The same id is Present when every listed file but info.json and
// thumb.png is byte equal, or for a reference when the library item has the same source kind and reference.
UnpackPlan planUnpack(const std::filesystem::path& projectFolder, const LibraryPaths& library);

struct UnpackOptions {
    bool keepLibrary = false; // on a conflict, keep the library's item and go on
};

struct UnpackReport {
    // planUnpack's errors, AlreadyExists naming the ids when there is a conflict and keepLibrary is off (nothing
    // is written then), CannotWrite, DatabaseError, Cancelled. On any failure everything added is removed again.
    Status status;
    std::vector<UnpackEntry> entries;
    int added = 0;
    bool complete = false; // nothing is missing or still to download
};

// Copies the pack's new items into the library and indexes them, all or nothing. Never downloads anything.
UnpackReport unpackProject(const std::filesystem::path& projectFolder, const LibraryPaths& library,
                           const UnpackOptions& options, std::stop_token stop = {});

// The manifest in the form of schema/pack.schema.json, and back. Reading checks the rules readPack lists.
Json packManifestToJson(const PackManifest& manifest);
PackRead packManifestFromJson(const Json& json);

// The results of "plynth pack" and "plynth unpack".
Json packReportToJson(const PackReport& report);
Json unpackReportToJson(const UnpackReport& report);

}
