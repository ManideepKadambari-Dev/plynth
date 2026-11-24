#include "PackSupport.h"

#include "plynth/core/ProjectFiles.h"
#include "plynth/core/catalog/Integrations.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <random>
#include <set>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// Text in lower case, for comparing names the way Windows does.
std::string lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// The kinds whose origin is known well enough to copy.
bool knownOwnSource(std::string_view kind)
{
    return kind == kSourceLocalFile || kind == kSourceTextureSet || kind == kSourceBlendMaterial;
}

// Adds one listed file when its path is safe and it is a plain file. False with why filled otherwise.
bool addListed(const fs::path& folder, const std::string& relative, std::set<std::string>& files, std::string* why)
{
    if (relative.empty()) return true;
    if (!pack::isSafeRelative(relative, why) || !pack::allowedFileType(relative)) {
        if (why && why->empty()) *why = "The file " + relative + " has a type a pack does not carry.";
        return false;
    }
    const fs::path file = folder / pathFromUtf8(relative);
    std::error_code ec;
    if (pack::isLink(file) || !fs::is_regular_file(file, ec)) {
        if (why) *why = "The file " + relative + " is missing.";
        return false;
    }
    files.insert(relative);
    return true;
}

// Adds a file only when it is there.
void addIfThere(const fs::path& folder, std::string_view relative, std::set<std::string>& files)
{
    const fs::path file = folder / pathFromUtf8(relative);
    std::error_code ec;
    if (!pack::isLink(file) && fs::is_regular_file(file, ec)) files.insert(std::string(relative));
}

// Adds every map of a set.
bool addMaps(const fs::path& folder, const MaterialMaps& maps, std::set<std::string>& files, std::string* why)
{
    for (const MapChannel channel : allMapChannels()) {
        if (!addListed(folder, materialMap(maps, channel).file, files, why)) return false;
    }
    return true;
}

}

// The names of the enums, as the manifest writes them.
std::string_view packKindName(PackKind kind)
{
    switch (kind) {
    case PackKind::Asset: return "asset";
    case PackKind::Material: return "material";
    case PackKind::Shape: return "shape";
    case PackKind::TrimProfile: return "trim_profile";
    case PackKind::Generator: return "generator";
    }
    return "asset";
}

std::optional<PackKind> packKindFromName(std::string_view name)
{
    for (PackKind kind : {PackKind::Asset, PackKind::Material, PackKind::Shape, PackKind::TrimProfile, PackKind::Generator}) {
        if (packKindName(kind) == name) return kind;
    }
    return std::nullopt;
}

std::string_view packStateName(PackState state)
{
    switch (state) {
    case PackState::Copied: return "copied";
    case PackState::Reference: return "reference";
    case PackState::Missing: return "missing";
    }
    return "missing";
}

std::optional<PackState> packStateFromName(std::string_view name)
{
    for (PackState state : {PackState::Copied, PackState::Reference, PackState::Missing}) {
        if (packStateName(state) == name) return state;
    }
    return std::nullopt;
}

std::string_view packReasonName(PackReason reason)
{
    switch (reason) {
    case PackReason::None: return "none";
    case PackReason::Store: return "store";
    case PackReason::License: return "license";
    case PackReason::UnknownOrigin: return "unknown_origin";
    case PackReason::Script: return "script";
    case PackReason::NotInLibrary: return "not_in_library";
    case PackReason::Broken: return "broken";
    }
    return "none";
}

std::optional<PackReason> packReasonFromName(std::string_view name)
{
    for (PackReason reason : {PackReason::None, PackReason::Store, PackReason::License, PackReason::UnknownOrigin,
                              PackReason::Script, PackReason::NotInLibrary, PackReason::Broken}) {
        if (packReasonName(reason) == name) return reason;
    }
    return std::nullopt;
}

std::string_view unpackActionName(UnpackAction action)
{
    switch (action) {
    case UnpackAction::Add: return "add";
    case UnpackAction::Present: return "present";
    case UnpackAction::Conflict: return "conflict";
    case UnpackAction::NeedsDownload: return "needs_download";
    case UnpackAction::Missing: return "missing";
    }
    return "missing";
}

// Decides whether an item may be copied into a pack, and why not when it may not.
PackReason packCopyRule(PackKind kind, const AssetSource& source, std::string_view license)
{
    if (kind == PackKind::Generator) return PackReason::Script;
    if (kind == PackKind::Shape || kind == PackKind::TrimProfile) return PackReason::None;
    // A store kind is any one a registered integration names, so a new store needs no change here.
    if (isStoreSource(source.kind) || source.kind == kSourceBlenderKit || source.kind == kSourceFabZip)
        return PackReason::Store;
    if (!knownOwnSource(source.kind)) return PackReason::UnknownOrigin;
    if (lower(source.reference) == kAssetBlendFileName) return PackReason::UnknownOrigin;
    if (license == "royalty_free" || license == "personal_use") return PackReason::License;
    return PackReason::None;
}

namespace pack {

std::string packPrefix(PackKind kind, const std::string& id)
{
    switch (kind) {
    case PackKind::Asset: return "assets/" + id + "/";
    case PackKind::Material: return "materials/" + id + "/";
    case PackKind::Shape:
    case PackKind::TrimProfile: return "shapes/";
    case PackKind::Generator: break;
    }
    return "generators/";
}

std::string shapeFileName(PackKind kind, const std::string& id)
{
    if (kind == PackKind::Shape) return id + std::string(kShapeSuffix);
    if (kind == PackKind::TrimProfile) return id + std::string(kTrimProfileSuffix);
    return {};
}

bool isSafeRelative(std::string_view text, std::string* why)
{
    const auto refuse = [&](const std::string& reason) {
        if (why) *why = "The path " + std::string(text) + " " + reason;
        return false;
    };
    if (text.empty()) return refuse("is empty.");
    if (text.size() > 240) return refuse("is too long.");
    for (const char c : text) {
        if (static_cast<unsigned char>(c) < 0x20) return refuse("holds a control character.");
        if (c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return refuse("holds a character a relative path may not have.");
    }
    if (text.front() == '/') return refuse("is not relative.");
    static constexpr std::array<std::string_view, 22> kReserved = {
        "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7",
        "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find('/', start), text.size());
        const std::string_view part = text.substr(start, end - start);
        if (part.empty()) return refuse("has an empty part.");
        if (part == "." || part == "..") return refuse("leaves its folder.");
        if (part.back() == '.' || part.back() == ' ') return refuse("has a part ending in a dot or a space.");
        const std::string base = lower(part.substr(0, part.find('.')));
        if (std::find(kReserved.begin(), kReserved.end(), base) != kReserved.end())
            return refuse("uses a name Windows reserves.");
        start = end + 1;
    }
    return true;
}

bool allowedFileType(std::string_view text)
{
    static constexpr std::array<std::string_view, 13> kTypes = {
        ".json", ".blend", ".fbx", ".png", ".jpg", ".jpeg", ".tga", ".tif", ".tiff", ".exr", ".hdr", ".bmp", ".webp"};
    const std::size_t dot = text.rfind('.');
    const std::size_t slash = text.rfind('/');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) return false;
    const std::string type = lower(text.substr(dot));
    return std::find(kTypes.begin(), kTypes.end(), type) != kTypes.end();
}

bool isLink(const fs::path& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

std::vector<std::string> assetFiles(const fs::path& folder, const AssetInfo& info, std::string* why)
{
    std::set<std::string> files;
    if (!addListed(folder, std::string(kAssetInfoFileName), files, why)) return {};
    if (!addListed(folder, std::string(kAssetBlendFileName), files, why)) return {};
    addIfThere(folder, kAssetFbxFileName, files);
    addIfThere(folder, kAssetThumbFileName, files);
    for (const std::string& texture : info.textures) {
        if (!addListed(folder, texture, files, why)) return {};
    }
    for (const AssetMaterial& material : info.materialMaps) {
        if (!addMaps(folder, material.maps, files, why) || !addMaps(folder, material.previewMaps, files, why)) return {};
    }
    return {files.begin(), files.end()};
}

std::vector<std::string> materialFiles(const fs::path& folder, const MaterialInfo& info, std::string* why)
{
    std::set<std::string> files;
    if (!addListed(folder, std::string(kMaterialInfoFileName), files, why)) return {};
    addIfThere(folder, kMaterialThumbFileName, files);
    if (!addMaps(folder, info.maps, files, why)) return {};
    return {files.begin(), files.end()};
}

bool copyKeepingTime(const fs::path& from, const fs::path& to)
{
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    if (!fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec) || ec) return false;
    const auto time = fs::last_write_time(from, ec);
    if (ec) return false;
    fs::last_write_time(to, time, ec);
    return !ec;
}

bool sameBytes(const fs::path& a, const fs::path& b)
{
    std::error_code ec;
    const auto sizeA = fs::file_size(a, ec);
    if (ec) return false;
    const auto sizeB = fs::file_size(b, ec);
    if (ec || sizeA != sizeB) return false;
    std::ifstream inA(a, std::ios::binary);
    std::ifstream inB(b, std::ios::binary);
    if (!inA || !inB) return false;
    std::array<char, 65536> bufA{};
    std::array<char, 65536> bufB{};
    while (inA && inB) {
        inA.read(bufA.data(), bufA.size());
        inB.read(bufB.data(), bufB.size());
        if (inA.gcount() != inB.gcount()) return false;
        if (!std::equal(bufA.begin(), bufA.begin() + inA.gcount(), bufB.begin())) return false;
    }
    return true;
}

std::string randomSuffix()
{
    std::random_device device;
    char text[9];
    std::snprintf(text, sizeof(text), "%08x", static_cast<unsigned>(device()));
    return text;
}

Json entryToJson(const PackEntry& entry)
{
    Json json = Json::object();
    json["kind"] = std::string(packKindName(entry.kind));
    json["id"] = entry.id;
    json["name"] = entry.name;
    json["state"] = std::string(packStateName(entry.state));
    if (entry.reason != PackReason::None) json["reason"] = std::string(packReasonName(entry.reason));
    if ((entry.kind == PackKind::Asset || entry.kind == PackKind::Material) && entry.state != PackState::Missing) {
        json["license"] = entry.license;
        json["source"] = Json{{"kind", entry.source.kind},
                              {"reference", entry.source.reference},
                              {"imported_at", formatSessionTime(entry.source.importedAt)}};
    }
    json["files"] = entry.files;
    return json;
}

}

}
