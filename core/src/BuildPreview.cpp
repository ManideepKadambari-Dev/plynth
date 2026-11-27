#include "plynth/core/BuildPreview.h"

#include "plynth/core/ProjectFiles.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace plynth {

namespace fs = std::filesystem;

// "storey", "slab", "roof" or "site".
std::string_view previewPartKindName(PreviewPartKind kind)
{
    switch (kind) {
    case PreviewPartKind::Roof: return "roof";
    case PreviewPartKind::Slab: return "slab";
    case PreviewPartKind::Site: return "site";
    case PreviewPartKind::Storey: break;
    }
    return "storey";
}

// The kind for a name used in files.
std::optional<PreviewPartKind> previewPartKindFromName(std::string_view name)
{
    if (name == "storey") return PreviewPartKind::Storey;
    if (name == "slab") return PreviewPartKind::Slab;
    if (name == "roof") return PreviewPartKind::Roof;
    if (name == "site") return PreviewPartKind::Site;
    return std::nullopt;
}

namespace {

// Says whether a text is a file name inside the folder: no folder part and ending in .fbx.
bool isPlainFbxName(const std::string& name)
{
    if (name.size() <= 4 || name.compare(name.size() - 4, 4, ".fbx") != 0) return false;
    return name.find_first_of("/\\") == std::string::npos;
}

}

// Reads build/preview/parts.json and makes the file paths absolute.
PreviewPartsRead readPreviewParts(const fs::path& buildFolder)
{
    PreviewPartsRead result;
    const fs::path folder = buildFolder / pathFromUtf8(kBuildPreviewFolder);
    const fs::path file = folder / pathFromUtf8(kPreviewPartsFileName);
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        result.status = fail(ErrorCode::NotFound, "There is no " + std::string(kPreviewPartsFileName) + " in " + pathToUtf8(folder) + ".");
        return result;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        result.status = fail(ErrorCode::CannotRead, "The file " + pathToUtf8(file) + " could not be opened.");
        return result;
    }
    const nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
    const auto bad = [&](const std::string& where) {
        result.status = fail(ErrorCode::BadFile, "The preview parts file is not valid: check " + where + ".", where);
        result.parts.clear();
        return result;
    };
    if (json.is_discarded() || !json.is_object()) return bad("");
    if (!json.contains("format") || json["format"] != std::string(kPreviewPartsFormat)) return bad("/format");
    if (!json.contains("version") || json["version"] != kPreviewPartsVersion) return bad("/version");
    if (!json.contains("parts") || !json["parts"].is_array()) return bad("/parts");

    const nlohmann::json& parts = json["parts"];
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const std::string where = "/parts/" + std::to_string(i);
        const nlohmann::json& p = parts[i];
        if (!p.is_object() || p.size() != 3) return bad(where);
        if (!p.contains("floor") || !p["floor"].is_string() || !isValidId(p["floor"].get<std::string>())) return bad(where + "/floor");
        if (!p.contains("kind") || !p["kind"].is_string()) return bad(where + "/kind");
        const auto kind = previewPartKindFromName(p["kind"].get<std::string>());
        if (!kind) return bad(where + "/kind");
        if (!p.contains("file") || !p["file"].is_string() || !isPlainFbxName(p["file"].get<std::string>())) return bad(where + "/file");
        PreviewPart part;
        part.floor = p["floor"].get<std::string>();
        part.kind = *kind;
        part.file = fs::absolute(folder / pathFromUtf8(p["file"].get<std::string>()), ec);
        if (!fs::is_regular_file(part.file, ec)) continue;
        result.parts.push_back(std::move(part));
    }
    return result;
}

}
