#include <plynth/core/catalog/FabZip.h>

#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/MaterialImporter.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <set>

namespace plynth {

namespace {

enum class WordKind { Plain, Format, Quality, Lod, LodNumber, Resolution, Poly };

struct Word {
    std::string text;
    WordKind kind = WordKind::Plain;
    int value = 0; // quality as int, LOD number, or pixels
};

std::string lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool isDigits(std::string_view text)
{
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

// The format group a word names, or empty.
std::string formatGroup(std::string_view word)
{
    if (word == "fbx") return "fbx";
    if (word == "gltf" || word == "glb") return "gltf";
    if (word == "obj") return "obj";
    if (word == "blend") return "blend";
    if (word == "usd" || word == "usda" || word == "usdc" || word == "usdz") return "usd";
    return {};
}

int qualityWord(std::string_view w)
{
    if (w == "low" || w == "lowpoly" || w == "lp") return static_cast<int>(FabQuality::Low);
    if (w == "medium" || w == "med" || w == "mid") return static_cast<int>(FabQuality::Medium);
    if (w == "high" || w == "highpoly" || w == "hp") return static_cast<int>(FabQuality::High);
    if (w == "raw" || w == "nanite" || w == "source" || w == "original") return static_cast<int>(FabQuality::Raw);
    return -1;
}

// Splits a path part into words on punctuation, spaces and lower to upper case changes. A piece that is a
// known whole word in any case (glTF, LowPoly) stays whole.
std::vector<std::string> splitWords(std::string_view part)
{
    std::vector<std::string> words;
    const auto addPiece = [&](const std::string& piece) {
        if (piece.empty()) return;
        const std::string l = lower(piece);
        if (!formatGroup(l).empty() || qualityWord(l) >= 0) {
            words.push_back(piece);
            return;
        }
        std::string current;
        for (const char ch : piece) {
            const auto c = static_cast<unsigned char>(ch);
            if (!current.empty() && c < 0x80 && std::isupper(c) && std::islower(static_cast<unsigned char>(current.back()))) {
                words.push_back(std::move(current));
                current.clear();
            }
            current += ch;
        }
        if (!current.empty()) words.push_back(std::move(current));
    };
    std::string piece;
    for (const char ch : part) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x80 && !std::isalnum(c)) {
            addPiece(piece);
            piece.clear();
        } else {
            piece += ch;
        }
    }
    addPiece(piece);
    return words;
}

int resolutionWord(std::string_view w)
{
    if (w == "05k") return 512;
    if (w == "1k") return 1024;
    if (w == "2k") return 2048;
    if (w == "4k") return 4096;
    if (w == "8k") return 8192;
    if (w == "16k") return 16384;
    for (const int n : {256, 512, 1024, 2048, 4096, 8192})
        if (w == std::to_string(n)) return n;
    return 0;
}

// Names the kind of each word of a path part.
std::vector<Word> classifyWords(std::string_view part)
{
    std::vector<Word> words;
    for (auto& text : splitWords(part)) words.push_back(Word{std::move(text), WordKind::Plain, 0});
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::string w = lower(words[i].text);
        if (w == "lod" && i + 1 < words.size() && isDigits(words[i + 1].text) && words[i + 1].text.size() < 4) {
            words[i].kind = WordKind::Lod;
            words[i].value = std::stoi(words[i + 1].text);
            words[i + 1].kind = WordKind::LodNumber;
            ++i;
        } else if (w.size() > 3 && w.size() < 7 && w.compare(0, 3, "lod") == 0 && isDigits(std::string_view(w).substr(3))) {
            words[i].kind = WordKind::Lod;
            words[i].value = std::stoi(w.substr(3));
        } else if (!formatGroup(w).empty()) {
            words[i].kind = WordKind::Format;
        } else if (qualityWord(w) >= 0) {
            words[i].kind = WordKind::Quality;
            words[i].value = qualityWord(w);
        } else if (w == "poly") {
            words[i].kind = WordKind::Poly;
        } else if (resolutionWord(w) > 0) {
            words[i].kind = WordKind::Resolution;
            words[i].value = resolutionWord(w);
        }
    }
    return words;
}

std::vector<std::string> pathParts(std::string_view entry)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= entry.size()) {
        std::size_t end = entry.find('/', start);
        if (end == std::string::npos) end = entry.size();
        if (end > start) parts.emplace_back(entry.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

std::string parentOf(const std::string& entry)
{
    const auto slash = entry.rfind('/');
    return slash == std::string::npos ? std::string() : entry.substr(0, slash);
}

std::string extensionOf(const std::string& entry)
{
    const auto slash = entry.rfind('/');
    const auto dot = entry.rfind('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    return lower(std::string_view(entry).substr(dot));
}

// The value of the word of the given kind nearest to the file name: the name first, then the folders up.
std::optional<int> nearestValue(const std::string& entry, WordKind kind)
{
    const auto parts = pathParts(entry);
    for (std::size_t p = parts.size(); p-- > 0;) {
        const auto words = classifyWords(parts[p]);
        for (std::size_t i = words.size(); i-- > 0;)
            if (words[i].kind == kind) return words[i].value;
    }
    return std::nullopt;
}

// The plain words of a path part, joined with "_".
std::string plainWords(const std::string& part)
{
    std::string out;
    for (const auto& w : classifyWords(part)) {
        if (w.kind != WordKind::Plain) continue;
        if (!out.empty()) out += '_';
        out += w.text;
    }
    return out;
}

// The stem of a model: its file name without the special words, else the nearest folder's.
std::string stemOf(const std::string& entry)
{
    const auto parts = pathParts(entry);
    for (std::size_t p = parts.size(); p-- > 0;) {
        const std::string stem = plainWords(parts[p]);
        if (!stem.empty()) return stem;
    }
    return "model";
}

std::string modelFormatOf(const std::string& ext)
{
    if (ext == ".fbx") return "fbx";
    if (ext == ".gltf" || ext == ".glb") return "gltf";
    if (ext == ".obj") return "obj";
    if (ext == ".blend") return "blend";
    return {};
}

bool isUnreadableModelExt(const std::string& ext)
{
    static const std::set<std::string> known = {".uasset", ".umap", ".usd", ".usda", ".usdc", ".usdz", ".unitypackage",
                                                ".max", ".ma", ".mb", ".c4d", ".3ds", ".dae", ".abc", ".stl", ".ply", ".spp"};
    return known.count(ext) != 0;
}

bool isImageExt(const std::string& ext)
{
    const auto all = textureImageSuffixes();
    return std::find(all.begin(), all.end(), ext) != all.end();
}

bool isPreview(const std::string& entry)
{
    for (const auto& part : pathParts(entry)) {
        for (const auto& w : splitWords(part)) {
            const std::string l = lower(w);
            if (l == "preview" || l == "previews" || l == "thumb" || l == "thumbnail" || l == "thumbnails" || l == "render" ||
                l == "renders" || l == "screenshot" || l == "screenshots" || l == "cover")
                return true;
        }
    }
    return false;
}

std::string joinNames(const std::vector<std::string>& names, std::size_t limit)
{
    std::string out;
    for (std::size_t i = 0; i < names.size() && i < limit; ++i) {
        if (i) out += ", ";
        out += names[i];
    }
    if (names.size() > limit) out += ", and " + std::to_string(names.size() - limit) + " more";
    return out;
}

// A path as UTF-8 text with forward slashes.
std::string utf8Slashes(const std::filesystem::path& path)
{
    std::string text = pathToUtf8(path);
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

Status invalid(std::string message) { return fail(ErrorCode::InvalidValue, std::move(message)); }

// Quality preference lists, nearest first.
std::array<FabQuality, 4> qualityOrder(FabQuality wanted)
{
    switch (wanted) {
    case FabQuality::Low: return {FabQuality::Low, FabQuality::Medium, FabQuality::High, FabQuality::Raw};
    case FabQuality::Medium: return {FabQuality::Medium, FabQuality::High, FabQuality::Low, FabQuality::Raw};
    case FabQuality::High: return {FabQuality::High, FabQuality::Raw, FabQuality::Medium, FabQuality::Low};
    case FabQuality::Raw: return {FabQuality::Raw, FabQuality::High, FabQuality::Medium, FabQuality::Low};
    }
    return {FabQuality::Medium, FabQuality::High, FabQuality::Low, FabQuality::Raw};
}

// Refuses an unsafe or encrypted entry of a kind that would be picked.
Status checkUnsafe(const FabZipContents& contents)
{
    for (const auto& entry : contents.unsafe) {
        const std::string ext = extensionOf(entry);
        if (isImageExt(ext) || !modelFormatOf(ext).empty() || ext == ".bin" || ext == ".mtl" || ext == ".json")
            return invalid("The zip holds " + entry + ", which cannot be unpacked safely.");
    }
    return {};
}

// Keeps the textures of the resolution to take. Untagged textures stay.
std::vector<FabTexture> filterByResolution(const std::vector<FabTexture>& textures, const std::optional<int>& maxSize,
                                           std::optional<int>& taken)
{
    std::set<int> found;
    for (const auto& t : textures)
        if (t.resolution) found.insert(*t.resolution);
    taken.reset();
    if (found.size() < 2) return textures;
    int pick = *found.rbegin();
    if (maxSize) {
        std::optional<int> best;
        for (const int r : found)
            if (r <= *maxSize) best = r;
        pick = best ? *best : *found.begin();
    }
    taken = pick;
    std::vector<FabTexture> out;
    for (const auto& t : textures)
        if (!t.resolution || *t.resolution == pick) out.push_back(t);
    return out;
}

// Adds the textures named in the maps option. A name the zip does not have is a refusal.
Status addPickedMaps(const FabZipContents& contents, const FabOptions& options, std::set<std::string>& taken)
{
    for (const auto& pick : options.maps) {
        const bool known = std::any_of(contents.textures.begin(), contents.textures.end(),
                                       [&](const FabTexture& t) { return t.entry == pick.entry; }) ||
                           std::find(contents.previews.begin(), contents.previews.end(), pick.entry) != contents.previews.end();
        if (!known) return invalid("The zip has no texture " + pick.entry + ".");
        taken.insert(pick.entry);
    }
    return {};
}

// The first .json by name that sits directly in one of the folders, tried in order.
std::optional<std::string> firstMetadata(const std::vector<std::string>& metadata, const std::vector<std::string>& folders)
{
    for (const auto& folder : folders) {
        for (const auto& entry : metadata)
            if (parentOf(entry) == folder) return entry;
    }
    return std::nullopt;
}

}

std::string_view fabQualityName(FabQuality quality)
{
    switch (quality) {
    case FabQuality::Low: return "low";
    case FabQuality::Medium: return "medium";
    case FabQuality::High: return "high";
    case FabQuality::Raw: return "raw";
    }
    return {};
}

std::optional<FabQuality> fabQualityFromName(std::string_view name)
{
    for (const auto q : {FabQuality::Low, FabQuality::Medium, FabQuality::High, FabQuality::Raw})
        if (fabQualityName(q) == name) return q;
    return std::nullopt;
}

Status fabOptionsFromJson(const Json& json, bool forImport, FabOptions& out)
{
    if (!json.is_object()) return invalid("The Fab options must be an object.");
    FabOptions next = out;
    for (const auto& [key, value] : json.items()) {
        if (key == "formats") {
            if (!value.is_array() || value.empty()) return invalid("The option formats must be a list with at least one format.");
            std::vector<std::string> formats;
            for (const auto& f : value) {
                const auto name = f.is_string() ? f.get<std::string>() : std::string();
                if (std::find(std::begin(kFabFormats), std::end(kFabFormats), name) == std::end(kFabFormats))
                    return invalid("The option formats may hold fbx, gltf, obj and blend only.");
                if (std::find(formats.begin(), formats.end(), name) != formats.end())
                    return invalid("The option formats lists " + name + " twice.");
                formats.push_back(name);
            }
            next.formats = std::move(formats);
        } else if (key == "quality") {
            const auto q = value.is_string() ? fabQualityFromName(value.get<std::string>()) : std::nullopt;
            if (!q) return invalid("The option quality must be low, medium, high or raw.");
            next.quality = *q;
        } else if (key == "max_texture_size") {
            if (value.is_null()) {
                next.maxTextureSize.reset();
            } else {
                if (!value.is_number_integer()) return invalid("The option max_texture_size must be a whole number or null.");
                const auto n = value.get<long long>();
                if (n < 256 || n > 16384) return invalid("The option max_texture_size must be from 256 to 16384.");
                next.maxTextureSize = static_cast<int>(n);
            }
        } else if (key == "normal") {
            const auto n = value.is_string() ? normalConventionFromName(value.get<std::string>()) : std::nullopt;
            if (!n) return invalid("The option normal must be auto, opengl or directx.");
            next.normal = *n;
        } else if (key == "license") {
            if (!value.is_string() || value.get<std::string>().empty()) return invalid("The option license must be a license name.");
            next.license = value.get<std::string>();
        } else if (key == "model") {
            if (!forImport) return invalid("The option model is for one import only and cannot be saved.");
            if (!value.is_string() || value.get<std::string>().empty()) return invalid("The option model must be a zip entry.");
            next.model = value.get<std::string>();
        } else if (key == "maps") {
            if (!forImport) return invalid("The option maps is for one import only and cannot be saved.");
            if (!value.is_object()) return invalid("The option maps must map material names to channels.");
            next.maps.clear();
            for (const auto& [material, channels] : value.items()) {
                if (!channels.is_object()) return invalid("The option maps for " + material + " must map channels to zip entries.");
                for (const auto& [channelName, entry] : channels.items()) {
                    const auto channel = mapChannelFromName(channelName);
                    if (!channel) return invalid("The option maps has an unknown channel: " + channelName + ".");
                    if (!entry.is_string() || entry.get<std::string>().empty())
                        return invalid("The option maps needs a zip entry for " + channelName + ".");
                    next.maps.push_back(FabMapPick{material, *channel, entry.get<std::string>()});
                }
            }
        } else {
            return invalid("Unknown Fab option: " + key + ".");
        }
    }
    out = std::move(next);
    return {};
}

Json fabOptionsToJson(const FabOptions& options)
{
    Json json = Json::object();
    json["formats"] = options.formats;
    json["quality"] = std::string(fabQualityName(options.quality));
    json["max_texture_size"] = options.maxTextureSize ? Json(*options.maxTextureSize) : Json(nullptr);
    json["normal"] = std::string(normalConventionName(options.normal));
    json["license"] = options.license;
    return json;
}

FabZipContents classifyFabZip(const std::vector<ZipEntry>& entries)
{
    FabZipContents out;
    for (const auto& e : entries) {
        if (e.unsafe || e.encrypted) {
            out.unsafe.push_back(e.name);
            continue;
        }
        if (e.folder) continue;
        const std::string ext = extensionOf(e.name);
        if (const std::string format = modelFormatOf(ext); !format.empty()) {
            FabModel m;
            m.entry = e.name;
            m.format = format;
            if (const auto q = nearestValue(e.name, WordKind::Quality)) m.quality = static_cast<FabQuality>(*q);
            m.lod = nearestValue(e.name, WordKind::Lod);
            m.stem = stemOf(e.name);
            out.models.push_back(std::move(m));
        } else if (isUnreadableModelExt(ext)) {
            out.unreadableModels.push_back(e.name);
        } else if (ext == ".zip") {
            out.innerZips.push_back(e.name);
        } else if (ext == ".json") {
            out.metadata.push_back(e.name);
        } else if (isImageExt(ext)) {
            if (isPreview(e.name)) {
                out.previews.push_back(e.name);
            } else {
                FabTexture t;
                t.entry = e.name;
                if (const auto r = nearestValue(e.name, WordKind::Resolution)) t.resolution = *r;
                out.textures.push_back(std::move(t));
            }
        } else {
            out.other.push_back(e.name);
        }
    }
    std::sort(out.models.begin(), out.models.end(), [](const FabModel& a, const FabModel& b) { return a.entry < b.entry; });
    std::sort(out.textures.begin(), out.textures.end(), [](const FabTexture& a, const FabTexture& b) { return a.entry < b.entry; });
    for (auto* list : {&out.previews, &out.unreadableModels, &out.innerZips, &out.metadata, &out.other, &out.unsafe})
        std::sort(list->begin(), list->end());
    return out;
}

bool looksLikeFabModelZip(const FabZipContents& contents)
{
    std::map<std::string, int> stems;
    for (const auto& m : contents.models) {
        if (++stems[lower(m.stem)] >= 2) return true;
        if (m.quality || m.lod) return true;
        const auto parts = pathParts(m.entry);
        for (std::size_t i = 0; i + 1 < parts.size(); ++i)
            for (const auto& w : splitWords(parts[i]))
                if (!formatGroup(lower(w)).empty()) return true;
    }
    return false;
}

bool looksLikeFabMaterialZip(const FabZipContents& contents)
{
    std::set<int> resolutions;
    for (const auto& t : contents.textures)
        if (t.resolution) resolutions.insert(*t.resolution);
    return resolutions.size() >= 2 || !contents.metadata.empty();
}

// Gives no item id yet.
std::string fabItemId(const FabZipContents&)
{
    return {};
}

FabChoice chooseFabModel(const FabZipContents& contents, const FabOptions& options)
{
    FabChoice choice;
    const FabModel* picked = nullptr;
    std::vector<std::string> allStems;
    std::set<std::string> seenStems;
    for (const auto& m : contents.models)
        if (seenStems.insert(lower(m.stem)).second) allStems.push_back(m.stem);

    if (!options.model.empty()) {
        for (const auto& m : contents.models)
            if (m.entry == options.model) picked = &m;
        if (!picked) {
            choice.status = invalid("The zip has no model " + options.model + ".");
            return choice;
        }
    } else {
        if (contents.models.empty()) {
            if (!contents.unreadableModels.empty()) {
                std::set<std::string> endings;
                for (const auto& e : contents.unreadableModels) endings.insert(extensionOf(e));
                choice.status = invalid("This zip holds no model Plynth can read (found: " +
                                        joinNames(std::vector<std::string>(endings.begin(), endings.end()), 10) +
                                        "). Download the FBX or glTF version from Fab.");
            } else if (!contents.innerZips.empty()) {
                choice.status = invalid("This zip holds other zip files. Unzip it and import the zip inside.");
            } else {
                choice.status = invalid("This zip holds no model Plynth can read (found: nothing). Download the FBX or glTF version from Fab.");
            }
            return choice;
        }

        std::vector<std::string> order = options.formats;
        for (const auto f : kFabFormats) {
            const std::string name(f);
            if (std::find(order.begin(), order.end(), name) == order.end()) order.push_back(name);
        }
        std::vector<const FabModel*> candidates;
        for (const auto& format : order) {
            for (const auto& m : contents.models)
                if (m.format == format) candidates.push_back(&m);
            if (!candidates.empty()) break;
        }

        std::vector<std::string> stems;
        std::set<std::string> seen;
        for (const auto* m : candidates)
            if (seen.insert(lower(m->stem)).second) stems.push_back(m->stem);
        if (stems.size() > 1) {
            choice.otherStems = stems;
            choice.status = invalid("This zip holds several models: " + joinNames(stems, 10) + ". Pick one with the option model.");
            return choice;
        }

        const auto order4 = qualityOrder(options.quality);
        const auto rank = [&](const FabModel* m) {
            if (!m->quality) return 4;
            return static_cast<int>(std::find(order4.begin(), order4.end(), *m->quality) - order4.begin());
        };
        const auto lodRank = [](const FabModel* m) { return m->lod ? *m->lod : 1000000; };
        picked = *std::min_element(candidates.begin(), candidates.end(), [&](const FabModel* a, const FabModel* b) {
            if (rank(a) != rank(b)) return rank(a) < rank(b);
            if (lodRank(a) != lodRank(b)) return lodRank(a) < lodRank(b);
            if (a->entry.size() != b->entry.size()) return a->entry.size() < b->entry.size();
            return a->entry < b->entry;
        });
    }

    choice.status = checkUnsafe(contents);
    if (!choice.status.ok()) return choice;
    choice.model = *picked;
    for (const auto& stem : allStems)
        if (lower(stem) != lower(picked->stem)) choice.otherStems.push_back(stem);

    std::set<std::string> files{picked->entry};
    const std::string folder = parentOf(picked->entry);
    const std::string companionExt = picked->format == "gltf" ? ".bin" : picked->format == "obj" ? ".mtl" : "";
    if (!companionExt.empty()) {
        for (const auto& e : contents.other)
            if (extensionOf(e) == companionExt && parentOf(e) == folder) files.insert(e);
    }

    // A texture belongs to another version when a folder below the search folder names another quality or format.
    const auto otherVariant = [&](const std::string& entry, const std::string& base) {
        const std::string relative = base.empty() ? entry : entry.substr(base.size() + 1);
        const auto parts = pathParts(relative);
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            for (const auto& w : classifyWords(parts[i])) {
                if (w.kind == WordKind::Quality && (!picked->quality || w.value != static_cast<int>(*picked->quality))) return true;
                if (w.kind == WordKind::Format && formatGroup(lower(w.text)) != picked->format) return true;
            }
        }
        return false;
    };

    std::vector<FabTexture> found;
    std::string textureFolder;
    bool haveTextures = false;
    std::set<std::string> variantSkipped;
    for (std::string dir = folder;; dir = parentOf(dir)) {
        const std::string prefix = dir.empty() ? std::string() : dir + "/";
        std::vector<FabTexture> here;
        for (const auto& t : contents.textures) {
            if (t.entry.compare(0, prefix.size(), prefix) != 0) continue;
            if (otherVariant(t.entry, dir)) variantSkipped.insert(t.entry);
            else here.push_back(t);
        }
        if (!here.empty()) {
            found = std::move(here);
            textureFolder = dir;
            haveTextures = true;
            break;
        }
        if (dir.empty()) break;
    }

    std::optional<int> resolution;
    const auto kept = filterByResolution(found, options.maxTextureSize, resolution);
    choice.textureResolution = resolution;
    std::set<std::string> taken;
    for (const auto& t : kept) taken.insert(t.entry);
    choice.status = addPickedMaps(contents, options, taken);
    if (!choice.status.ok()) return choice;

    std::vector<std::string> metadataFolders{folder};
    if (haveTextures) metadataFolders.push_back(textureFolder);
    for (std::string dir = folder; !dir.empty();) {
        dir = parentOf(dir);
        metadataFolders.push_back(dir);
    }
    choice.metadata = firstMetadata(contents.metadata, metadataFolders);
    if (choice.metadata) files.insert(*choice.metadata);
    for (const auto& t : taken) files.insert(t);
    choice.textures.assign(taken.begin(), taken.end());
    choice.files.assign(files.begin(), files.end());

    for (const auto& m : contents.models)
        if (m.entry != picked->entry) choice.skipped.emplace_back(m.entry, "another version of the model");
    for (const auto& t : contents.textures) {
        if (taken.count(t.entry)) continue;
        if (variantSkipped.count(t.entry)) choice.skipped.emplace_back(t.entry, "texture of another version");
        else if (std::any_of(found.begin(), found.end(), [&](const FabTexture& f) { return f.entry == t.entry; }))
            choice.skipped.emplace_back(t.entry, "another resolution");
        else choice.skipped.emplace_back(t.entry, "not next to the model");
    }
    for (const auto& e : contents.unreadableModels) choice.skipped.emplace_back(e, "model format Plynth cannot read");
    for (const auto& e : contents.previews) choice.skipped.emplace_back(e, "preview picture");
    return choice;
}

FabChoice chooseFabMaterial(const FabZipContents& contents, const FabOptions& options)
{
    FabChoice choice;
    if (contents.textures.empty()) {
        choice.status = invalid("This zip holds no texture images.");
        return choice;
    }
    choice.status = checkUnsafe(contents);
    if (!choice.status.ok()) return choice;

    std::optional<int> resolution;
    const auto kept = filterByResolution(contents.textures, options.maxTextureSize, resolution);
    choice.textureResolution = resolution;
    std::set<std::string> taken;
    for (const auto& t : kept) taken.insert(t.entry);
    choice.status = addPickedMaps(contents, options, taken);
    if (!choice.status.ok()) return choice;

    if (!contents.metadata.empty()) choice.metadata = contents.metadata.front();
    std::set<std::string> files(taken);
    if (choice.metadata) files.insert(*choice.metadata);
    choice.textures.assign(taken.begin(), taken.end());
    choice.files.assign(files.begin(), files.end());
    for (const auto& t : contents.textures)
        if (!taken.count(t.entry)) choice.skipped.emplace_back(t.entry, "another resolution");
    for (const auto& e : contents.previews) choice.skipped.emplace_back(e, "preview picture");
    return choice;
}

Json fabSourceJson(const FabChoice& choice, std::string_view kind, const std::filesystem::path& zip,
                   const std::filesystem::path& unpackFolder, const FabOptions& options)
{
    Json maps = Json::object();
    for (const auto& pick : options.maps) {
        if (!maps.contains(pick.material)) maps[pick.material] = Json::object();
        maps[pick.material][std::string(mapChannelName(pick.channel))] = pick.entry;
    }
    Json json = Json::object();
    json["format"] = std::string(kFabSourceFormat);
    json["version"] = kFabSourceVersion;
    json["zip"] = utf8Slashes(zip);
    json["kind"] = std::string(kind);
    json["unpack_folder"] = utf8Slashes(unpackFolder);
    json["files"] = choice.files;
    json["model"] = choice.model ? Json(choice.model->entry) : Json(nullptr);
    json["model_format"] = choice.model ? Json(choice.model->format) : Json(nullptr);
    json["textures"] = choice.textures;
    json["metadata"] = choice.metadata ? Json(*choice.metadata) : Json(nullptr);
    json["maps"] = std::move(maps);
    json["normal"] = std::string(normalConventionName(options.normal));
    return json;
}

Json fabChoiceDetails(const FabZipContents& contents, const FabChoice& choice)
{
    Json models = Json::array();
    for (const auto& m : contents.models) {
        Json item = Json::object();
        item["entry"] = m.entry;
        item["format"] = m.format;
        item["quality"] = m.quality ? Json(std::string(fabQualityName(*m.quality))) : Json(nullptr);
        item["lod"] = m.lod ? Json(*m.lod) : Json(nullptr);
        item["stem"] = m.stem;
        models.push_back(std::move(item));
    }
    Json skipped = Json::array();
    for (const auto& [entry, reason] : choice.skipped) {
        Json item = Json::object();
        item["entry"] = entry;
        item["reason"] = reason;
        skipped.push_back(std::move(item));
    }

    Json json = Json::object();
    json["models"] = std::move(models);
    json["picked"] = choice.model ? Json(choice.model->entry) : Json(nullptr);
    json["textures"] = choice.textures;
    json["texture_resolution"] = choice.textureResolution ? Json(*choice.textureResolution) : Json(nullptr);
    json["metadata"] = choice.metadata ? Json(*choice.metadata) : Json(nullptr);
    json["skipped"] = std::move(skipped);
    json["other_models"] = choice.otherStems;
    json["unreadable"] = contents.unreadableModels;
    json["previews"] = contents.previews;
    return json;
}

}
