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

// The text in lower case. Only the letters A to Z change.
std::string lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Says whether text is one or more digits and nothing else.
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

// The quality a lower case word names, as a number, or -1 for none.
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

// The pixels across a lower case resolution word names, or 0 for none.
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

// The folder and file names of a zip entry, top down, without empty ones.
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

// The folder of a zip entry, empty at the top of the zip.
std::string parentOf(const std::string& entry)
{
    const auto slash = entry.rfind('/');
    return slash == std::string::npos ? std::string() : entry.substr(0, slash);
}

// The ending of a zip entry in lower case with its dot, empty for none.
std::string extensionOf(const std::string& entry)
{
    const auto slash = entry.rfind('/');
    const auto dot = entry.rfind('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    return lower(std::string_view(entry).substr(dot));
}

// The file name of a zip entry without its ending.
std::string fileStemOf(const std::string& entry)
{
    const auto parts = pathParts(entry);
    if (parts.empty()) return {};
    std::string name = parts.back();
    const std::string ext = extensionOf(entry);
    if (ext.size() <= name.size()) name.erase(name.size() - ext.size());
    return name;
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

// Says whether a lower case word is one of the general words that name no item, such as model or mesh.
bool isGeneralWord(const std::string& word)
{
    static const std::set<std::string> general{"model",  "models",  "mesh",     "meshes", "scene",  "scenes", "object",  "objects",
                                               "geometry", "asset", "export", "exports", "file", "files", "untitled"};
    return general.count(word) != 0;
}

// The item of a model, the name it goes by: its stem, or for a general stem the nearest folder with its own words.
std::string itemOf(const std::string& entry, const std::string& stem)
{
    if (!isGeneralWord(lower(stem))) return stem;
    const auto parts = pathParts(entry);
    for (std::size_t p = parts.empty() ? 0 : parts.size() - 1; p-- > 0;) {
        for (const auto& w : classifyWords(parts[p])) {
            if (w.kind == WordKind::Plain && !isGeneralWord(lower(w.text))) return plainWords(parts[p]);
        }
    }
    return stem;
}

// The lower case words of an item.
std::set<std::string> itemWordsOf(const std::string& item)
{
    std::set<std::string> words;
    for (const auto& w : splitWords(item)) words.insert(lower(w));
    return words;
}

// The plain words of a folder name that tell one model from another: not general and not the item's own.
std::vector<std::string> ownFolderWords(const std::string& part, const std::set<std::string>& itemWords)
{
    std::vector<std::string> out;
    for (const auto& w : classifyWords(part)) {
        const std::string l = lower(w.text);
        if (w.kind == WordKind::Plain && !isGeneralWord(l) && itemWords.count(l) == 0) out.push_back(w.text);
    }
    return out;
}

// The folder words of a model, top down and in lower case.
std::vector<std::string> folderWordsOf(const std::string& entry, const std::string& item)
{
    const auto itemWords = itemWordsOf(item);
    const auto parts = pathParts(entry);
    std::vector<std::string> out;
    for (std::size_t p = 0; p + 1 < parts.size(); ++p)
        for (const auto& w : ownFolderWords(parts[p], itemWords)) out.push_back(lower(w));
    return out;
}

// The folders of a model that hold its folder words, each with only those words and a "/" after it.
std::string folderPrefix(const std::string& entry, const std::string& item)
{
    const auto itemWords = itemWordsOf(item);
    const auto parts = pathParts(entry);
    std::string out;
    for (std::size_t p = 0; p + 1 < parts.size(); ++p) {
        const auto words = ownFolderWords(parts[p], itemWords);
        if (words.empty()) continue;
        for (std::size_t i = 0; i < words.size(); ++i) {
            if (i) out += '_';
            out += words[i];
        }
        out += '/';
    }
    return out;
}

// One different model of a zip with all its versions.
struct ModelGroup {
    std::string item;
    std::vector<std::string> folderWords;
    std::vector<const FabModel*> models;
    std::string shown;
};

// Sorts the models into different models, in the order of their first entry, each with its shown name.
std::vector<ModelGroup> groupModels(const std::vector<FabModel>& models)
{
    std::vector<ModelGroup> groups;
    for (const auto& m : models) {
        const auto words = folderWordsOf(m.entry, m.item);
        const std::string key = lower(m.item);
        const auto same = std::find_if(groups.begin(), groups.end(),
                                       [&](const ModelGroup& g) { return lower(g.item) == key && g.folderWords == words; });
        if (same == groups.end()) groups.push_back(ModelGroup{m.item, words, {&m}, m.item});
        else same->models.push_back(&m);
    }
    for (auto& g : groups) {
        const bool shared = std::any_of(groups.begin(), groups.end(),
                                        [&](const ModelGroup& o) { return &o != &g && lower(o.item) == lower(g.item); });
        if (shared) g.shown = folderPrefix(g.models.front()->entry, g.item) + g.item;
    }
    return groups;
}

// The model format of an ending, empty for one Plynth cannot read.
std::string modelFormatOf(const std::string& ext)
{
    if (ext == ".fbx") return "fbx";
    if (ext == ".gltf" || ext == ".glb") return "gltf";
    if (ext == ".obj") return "obj";
    if (ext == ".blend") return "blend";
    return {};
}

// Says whether an ending is a model format Plynth knows of but cannot read.
bool isUnreadableModelExt(const std::string& ext)
{
    static const std::set<std::string> known = {".uasset", ".umap", ".usd", ".usda", ".usdc", ".usdz", ".unitypackage",
                                                ".max", ".ma", ".mb", ".c4d", ".3ds", ".dae", ".abc", ".stl", ".ply", ".spp"};
    return known.count(ext) != 0;
}

// Says whether an ending is one of the texture image endings.
bool isImageExt(const std::string& ext)
{
    const auto all = textureImageSuffixes();
    return std::find(all.begin(), all.end(), ext) != all.end();
}

// Says whether a quality word of the path, other than source and original, marks it as a Fab version.
bool hasVersionQualityWord(const std::string& entry)
{
    for (const auto& part : pathParts(entry)) {
        for (const auto& w : classifyWords(part)) {
            const std::string l = lower(w.text);
            if (w.kind == WordKind::Quality && l != "source" && l != "original") return true;
        }
    }
    return false;
}

// Says whether an image is a preview picture rather than a texture.
bool isPreview(const std::string& entry)
{
    static const std::set<std::string> names{"preview", "previews", "thumb",      "thumbs",      "thumbnail", "thumbnails",
                                             "render",  "renders",  "screenshot", "screenshots", "cover",     "covers"};
    static const std::set<std::string> firstNames{"preview",    "previews",   "thumb",      "thumbs",
                                                  "thumbnail", "thumbnails", "screenshot", "screenshots"};
    const auto parts = pathParts(entry);
    for (std::size_t p = 0; p + 1 < parts.size(); ++p)
        if (names.count(lower(parts[p]))) return true;
    std::vector<std::string> words;
    for (const auto& w : splitWords(fileStemOf(entry))) {
        const std::string l = lower(w);
        if (!isDigits(l) && resolutionWord(l) == 0) words.push_back(l);
    }
    if (words.empty()) return false;
    return names.count(words.back()) != 0 || firstNames.count(words.front()) != 0;
}

// The names joined with commas, at most limit of them, then how many more there are.
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

// A refusal with this sentence.
Status invalid(std::string message) { return fail(ErrorCode::InvalidValue, std::move(message)); }

// Says whether text is a license id as the schema allows: a lower case letter, then letters, digits and "_".
bool isLicenseId(const std::string& text)
{
    if (text.empty() || text.size() > 64 || text[0] < 'a' || text[0] > 'z') return false;
    return std::all_of(text.begin(), text.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

// Says whether text could be a Fab item id: six to ten lower case letters and digits.
bool looksLikeItemId(const std::string& text)
{
    if (text.size() < 6 || text.size() > 10) return false;
    return std::all_of(text.begin(), text.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); });
}

// Says whether a word of an entry's file name is this word, ignoring case.
bool fileNameHasWord(const std::string& entry, const std::string& word)
{
    const auto parts = pathParts(entry);
    if (parts.empty()) return false;
    for (const auto& w : splitWords(parts.back()))
        if (lower(w) == word) return true;
    return false;
}

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

// The ending of an entry once a stream name and trailing dots and spaces are left out, as Windows would see it.
std::string hiddenEnding(const std::string& entry)
{
    std::string name = entry;
    const auto slash = name.rfind('/');
    const auto colon = name.find(':', slash == std::string::npos ? 0 : slash + 1);
    if (colon != std::string::npos) name.erase(colon);
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    return extensionOf(name);
}

// Says whether an entry with this ending is of a kind that would be given to the worker.
bool isPickedKind(const std::string& ext)
{
    return isImageExt(ext) || !modelFormatOf(ext).empty() || ext == ".bin" || ext == ".mtl" || ext == ".json";
}

// The refusal of an entry that cannot be unpacked safely.
Status unsafeEntry(const std::string& entry) { return invalid("The zip holds " + entry + ", which cannot be unpacked safely."); }

// Says whether the entry is among the unsafe or encrypted ones.
bool isUnsafeEntry(const FabZipContents& contents, const std::string& entry)
{
    return std::find(contents.unsafe.begin(), contents.unsafe.end(), entry) != contents.unsafe.end();
}

// Refuses an unsafe or encrypted entry of a kind that would be picked, also behind a hidden ending.
Status checkUnsafe(const FabZipContents& contents)
{
    for (const auto& entry : contents.unsafe)
        if (isPickedKind(extensionOf(entry)) || isPickedKind(hiddenEnding(entry))) return unsafeEntry(entry);
    return {};
}

// What tells one texture from another: its path without resolution words, without folders made only of them
// and without its ending, in lower case and without separators.
std::string textureKey(const std::string& entry)
{
    auto parts = pathParts(entry);
    if (parts.empty()) return {};
    parts.back() = fileStemOf(entry);
    std::string key;
    for (const auto& part : parts) {
        std::string piece;
        bool hadResolution = false;
        for (const auto& w : classifyWords(part)) {
            if (w.kind == WordKind::Resolution) hadResolution = true;
            else piece += lower(w.text);
        }
        if (piece.empty() && hadResolution) continue;
        key += piece;
        key += '/';
    }
    return key;
}

// Keeps one resolution of each texture: its largest up to maxSize, its largest when there is no limit, its
// smallest when all are larger. Textures without a resolution word stay. largest is the largest resolution
// taken when the textures came in several, else empty.
std::vector<FabTexture> filterByResolution(const std::vector<FabTexture>& textures, const std::optional<int>& maxSize,
                                           std::optional<int>& largest)
{
    std::set<int> found;
    std::map<std::string, std::set<int>> sizesOf;
    for (const auto& t : textures) {
        auto& sizes = sizesOf[textureKey(t.entry)];
        if (t.resolution) {
            found.insert(*t.resolution);
            sizes.insert(*t.resolution);
        }
    }
    std::map<std::string, int> pickOf;
    for (const auto& [key, sizes] : sizesOf) {
        if (sizes.empty()) continue;
        int pick = *sizes.rbegin();
        if (maxSize) {
            std::optional<int> best;
            for (const int r : sizes)
                if (r <= *maxSize) best = r;
            pick = best ? *best : *sizes.begin();
        }
        pickOf[key] = pick;
    }
    largest.reset();
    std::vector<FabTexture> out;
    for (const auto& t : textures) {
        if (t.resolution && pickOf[textureKey(t.entry)] != *t.resolution) continue;
        out.push_back(t);
        if (found.size() >= 2 && t.resolution && (!largest || *t.resolution > *largest)) largest = t.resolution;
    }
    return out;
}

// Adds the textures named in the maps option. A name the zip does not have, or an unsafe one, is a refusal.
Status addPickedMaps(const FabZipContents& contents, const FabOptions& options, std::set<std::string>& taken)
{
    for (const auto& pick : options.maps) {
        if (isUnsafeEntry(contents, pick.entry)) return unsafeEntry(pick.entry);
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

// The option word of a quality.
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

// The quality an option word names, empty for an unknown word.
std::optional<FabQuality> fabQualityFromName(std::string_view name)
{
    for (const auto q : {FabQuality::Low, FabQuality::Medium, FabQuality::High, FabQuality::Raw})
        if (fabQualityName(q) == name) return q;
    return std::nullopt;
}

// Reads the Fab options from JSON, refusing unknown keys and bad values with a sentence naming the option.
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
            if (!value.is_string() || !isLicenseId(value.get<std::string>()))
                return invalid("The option license must be a license id: a lower case letter, then up to 63 lower case letters, "
                               "digits and _.");
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

// The saved Fab options as JSON, without the options for one import.
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

// Sorts the entries of a zip into models, textures, previews and the rest.
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
            m.item = itemOf(e.name, m.stem);
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

// Says whether the models look like the versions of a Fab download.
bool looksLikeFabModelZip(const FabZipContents& contents)
{
    std::map<std::string, int> stems;
    for (const auto& m : contents.models) {
        if (++stems[lower(m.stem)] >= 2) return true;
        if (m.lod || hasVersionQualityWord(m.entry)) return true;
        const auto parts = pathParts(m.entry);
        for (std::size_t i = 0; i + 1 < parts.size(); ++i)
            for (const auto& w : splitWords(parts[i]))
                if (!formatGroup(lower(w)).empty()) return true;
    }
    return false;
}

// Says whether the textures look like a Fab download rather than the user's own texture set.
bool looksLikeFabMaterialZip(const FabZipContents& contents)
{
    for (const auto& entry : contents.metadata) {
        const auto parts = pathParts(entry);
        if (!parts.empty() && lower(parts.back()) == "material.json") return false;
    }
    std::set<int> resolutions;
    for (const auto& t : contents.textures)
        if (t.resolution) resolutions.insert(*t.resolution);
    return resolutions.size() >= 2 || !contents.metadata.empty();
}

// The Fab id of the zip's item, only when a .json of that name is there and the id is a word of a model or
// texture file name. Empty when there is none.
std::string fabItemId(const FabZipContents& contents)
{
    std::vector<std::string> ids;
    for (const auto& entry : contents.metadata) {
        const std::string id = fileStemOf(entry);
        if (!looksLikeItemId(id)) continue;
        const bool used = std::any_of(contents.models.begin(), contents.models.end(),
                                      [&](const FabModel& m) { return fileNameHasWord(m.entry, id); }) ||
                          std::any_of(contents.textures.begin(), contents.textures.end(),
                                      [&](const FabTexture& t) { return fileNameHasWord(t.entry, id); });
        if (used) ids.push_back(id);
    }
    if (ids.empty()) return {};
    return *std::min_element(ids.begin(), ids.end());
}

// Picks the model version and the textures that go with it, or refuses with a plain sentence.
FabChoice chooseFabModel(const FabZipContents& contents, const FabOptions& options)
{
    FabChoice choice;
    const FabModel* picked = nullptr;
    const auto groups = groupModels(contents.models);

    if (!options.model.empty()) {
        for (const auto& m : contents.models)
            if (m.entry == options.model) picked = &m;
        if (!picked) {
            choice.status = isUnsafeEntry(contents, options.model) ? unsafeEntry(options.model)
                                                                   : invalid("The zip has no model " + options.model + ".");
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

        if (groups.size() > 1) {
            for (const auto& g : groups) choice.otherStems.push_back(g.shown);
            choice.status = invalid("This zip holds several models: " + joinNames(choice.otherStems, 10) + ". Pick one with the option model.");
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
    for (const auto& g : groups)
        if (std::find(g.models.begin(), g.models.end(), picked) == g.models.end()) choice.otherStems.push_back(g.shown);

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

// Picks the textures of a material zip, one resolution of each.
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

// The source file the worker reads to unpack and build the picked entries.
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

// What was found and picked in a zip, for the dry run and the pick of a model.
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
        item["item"] = m.item;
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
