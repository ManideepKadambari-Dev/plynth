#include "plynth/core/catalog/BlenderKit.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <set>
#include <string>

namespace plynth {

namespace {

const char* const kNotAnAddress = "This is not a BlenderKit address.";
const char* const kNoOneItem = "The address does not name one BlenderKit item. Copy it from the item's page.";
const char* const kOnlyKinds = "Only models and materials can be imported from BlenderKit.";

// Lower case copy of ASCII text.
std::string lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Removes spaces, tabs and line ends from both ends.
std::string_view trim(std::string_view text)
{
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && space(text.front())) text.remove_prefix(1);
    while (!text.empty() && space(text.back())) text.remove_suffix(1);
    return text;
}

// Says whether a character is a hex digit.
bool isHex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Whether the text holds 8-4-4-4-12 hex digits from pos.
bool idAt(std::string_view text, std::size_t pos)
{
    if (pos + 36 > text.size()) return false;
    for (std::size_t i = 0; i < 36; ++i) {
        const char c = text[pos + i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!isHex(c)) {
            return false;
        }
    }
    return true;
}

// Every different id in the text, lower case. An id inside a longer run of hex digits does not count.
std::set<std::string> idsIn(std::string_view text)
{
    std::set<std::string> found;
    for (std::size_t i = 0; i + 36 <= text.size(); ++i) {
        if (!idAt(text, i)) continue;
        const bool before = i > 0 && (isHex(text[i - 1]) || text[i - 1] == '-');
        const bool after = i + 36 < text.size() && (isHex(text[i + 36]) || text[i + 36] == '-');
        if (before || after) continue;
        found.insert(lower(text.substr(i, 36)));
        i += 35;
    }
    return found;
}

// The answer for a text that must hold exactly one id.
BlenderKitAddress oneId(std::string_view text)
{
    const std::set<std::string> ids = idsIn(text);
    if (ids.size() != 1) return {fail(ErrorCode::InvalidValue, kNoOneItem), {}};
    return {{}, *ids.begin()};
}

}

// Says whether a texture resolution is one the add-on offers.
bool isBlenderKitResolution(std::string_view resolution)
{
    for (const char* known : {"0.5k", "1k", "2k", "4k", "8k", "original"}) {
        if (resolution == known) return true;
    }
    return false;
}

// Reads the item id from what the user pasted.
BlenderKitAddress parseBlenderKitAddress(std::string_view pasted)
{
    const std::string_view text = trim(pasted);
    const std::string low = lower(text);
    if (text.size() == 36 && idAt(text, 0)) return {{}, low};
    if (low.rfind("asset_base_id:", 0) == 0) {
        BlenderKitAddress address = oneId(text);
        if (!address.status.ok()) return address;
        // The add-on copies the item's type after the id. Only a model or a material may go on to Blender.
        if (const std::size_t type = low.find("asset_type:"); type != std::string::npos) {
            const std::size_t begin = type + 11;
            const std::string word = low.substr(begin, low.find_first_of(" \t\r\n", begin) - begin);
            if (word != "model" && word != "material") return {fail(ErrorCode::InvalidValue, kOnlyKinds), {}};
        }
        return address;
    }
    std::size_t start = 0;
    if (low.rfind("https://", 0) == 0) start = 8;
    else if (low.rfind("http://", 0) == 0) start = 7;
    else return {fail(ErrorCode::InvalidValue, kNotAnAddress), {}};

    const std::size_t hostEnd = low.find_first_of("/?#", start);
    std::string host = low.substr(start, hostEnd == std::string::npos ? std::string::npos : hostEnd - start);
    if (const std::size_t at = host.rfind('@'); at != std::string::npos) host.erase(0, at + 1);
    if (const std::size_t colon = host.find(':'); colon != std::string::npos) host.erase(colon);
    if (host != "blenderkit.com" && host != "www.blenderkit.com") return {fail(ErrorCode::InvalidValue, kNotAnAddress), {}};
    if (hostEnd == std::string::npos) return {fail(ErrorCode::InvalidValue, kNoOneItem), {}};
    const std::size_t pathEnd = low.find_first_of("?#", hostEnd);
    return oneId(text.substr(hostEnd, pathEnd == std::string::npos ? std::string::npos : pathEnd - hostEnd));
}

// Reads the BlenderKit options.
Status blenderKitOptionsFromJson(const Json& json, BlenderKitOptions& out)
{
    if (!json.is_object()) return fail(ErrorCode::InvalidValue, "The BlenderKit options must be an object.");
    BlenderKitOptions result = out;
    for (const auto& item : json.items()) {
        const std::string& key = item.key();
        const Json& value = item.value();
        if (key == "blender_window") {
            if (!value.is_string() || (value.get<std::string>() != "none" && value.get<std::string>() != "minimized")) {
                return fail(ErrorCode::InvalidValue, "blender_window must be \"none\" or \"minimized\".");
            }
            // Read and ignored: older files may hold "minimized", and fetches always run in the background.
        } else if (key == "resolution") {
            if (!value.is_string() || !isBlenderKitResolution(value.get<std::string>())) {
                return fail(ErrorCode::InvalidValue, "resolution must be 0.5k, 1k, 2k, 4k, 8k or original.");
            }
            result.resolution = value.get<std::string>();
        } else if (key == "wait_seconds") {
            if (!value.is_number_integer() || value.get<long long>() < 60 || value.get<long long>() > 3600) {
                return fail(ErrorCode::InvalidValue, "wait_seconds must be a whole number from 60 to 3600.");
            }
            result.waitSeconds = value.get<int>();
        } else {
            return fail(ErrorCode::InvalidValue, "BlenderKit has no option called " + key + ".");
        }
    }
    out = result;
    return {};
}

// Builds the fetch for one item.
ImportFetch makeBlenderKitFetch(std::string_view assetBaseId, std::string_view kind, const BlenderKitOptions& options)
{
    ImportFetch fetch;
    fetch.task = std::string(kBlenderKitFetchTask);
    fetch.params = Json::object();
    fetch.params["asset_base_id"] = std::string(assetBaseId);
    fetch.params["kind"] = std::string(kind);
    fetch.params["resolution"] = options.resolution;
    fetch.params["wait_seconds"] = options.waitSeconds;
    fetch.expectedKind = std::string(kind);
    fetch.timeout = std::chrono::seconds(options.waitSeconds + 120);
    fetch.steps = {{"check_addon", "Checking the BlenderKit add-on"},
                   {"download", "Downloading from BlenderKit"},
                   {"copy", "Copying the download"}};
    return fetch;
}

}
