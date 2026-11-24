#include "plynth/core/Tour.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <set>

namespace plynth {

using Json = nlohmann::ordered_json;

namespace {

constexpr std::array<std::string_view, 5> kTourIds = {"plan", "wall", "build_export", "assets", "materials"};
constexpr std::array<std::string_view, 7> kScreens = {"home", "plan", "wall", "build", "export", "assets", "materials"};

// Thrown while reading, with a JSON pointer to the bad value.
struct TourProblem {
    std::string where;
    std::string message;
};

// Escapes a key for a JSON pointer.
std::string pointerKey(const std::string& key)
{
    std::string out;
    for (char c : key) {
        if (c == '~') out += "~0";
        else if (c == '/') out += "~1";
        else out.push_back(c);
    }
    return out;
}

// The number of characters (not bytes) in UTF-8 text.
std::size_t characterCount(std::string_view text)
{
    return static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](char c) {
        return (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    }));
}

// Says whether text uses a dash as punctuation: an em or en dash, a double dash or a lone hyphen between spaces.
bool hasPunctuationDash(std::string_view text)
{
    return text.find("\xE2\x80\x94") != std::string_view::npos || text.find("\xE2\x80\x93") != std::string_view::npos ||
           text.find("--") != std::string_view::npos || text.find(" - ") != std::string_view::npos ||
           text.starts_with("- ") || text.ends_with(" -");
}

// The number of sentences: each ".", "!" or "?" that ends the text or is followed by a space.
int sentenceCount(std::string_view text)
{
    int count = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c != '.' && c != '!' && c != '?') continue;
        if (i + 1 == text.size() || text[i + 1] == ' ') ++count;
    }
    if (count == 0 && !text.empty()) count = 1;
    return count;
}

// Text at key that is not empty, at most maxLength characters and has no dash used as punctuation.
std::string checkedText(const Json& object, const char* key, const std::string& pointer, std::size_t maxLength)
{
    const std::string where = pointer + "/" + key;
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        throw TourProblem{where, std::string("The value \"") + key + "\" must be text."};
    }
    std::string text = it->get<std::string>();
    if (text.find_first_not_of(' ') == std::string::npos) throw TourProblem{where, "The text may not be empty."};
    if (characterCount(text) > maxLength) {
        throw TourProblem{where, "The text is longer than " + std::to_string(maxLength) + " characters."};
    }
    if (hasPunctuationDash(text)) throw TourProblem{where, "The text uses a dash as punctuation."};
    return text;
}

// Says whether a wildcard key covers an id.
bool wildcardCovers(const std::string& key, std::string_view id)
{
    return !key.empty() && key.back() == '*' && id.starts_with(std::string_view(key).substr(0, key.size() - 1));
}

// Reads every value of the tour file.
TourData readData(const Json& json)
{
    TourData data;
    const auto controls = json.find("controls");
    if (controls == json.end() || !controls->is_object()) {
        throw TourProblem{"/controls", "The value \"controls\" must be a JSON object."};
    }
    for (auto it = controls->begin(); it != controls->end(); ++it) {
        const std::string pointer = "/controls/" + pointerKey(it.key());
        if (it.key().empty() || it.key() == "*") throw TourProblem{pointer, "A control id may not be empty."};
        if (!it->is_object()) throw TourProblem{pointer, "Each control must be a JSON object."};
        TourEntry entry;
        entry.id = it.key();
        entry.name = checkedText(*it, "name", pointer, kMaxTourNameLength);
        entry.text = checkedText(*it, "text", pointer, kMaxTourTextLength);
        if (sentenceCount(entry.text) > 2) throw TourProblem{pointer + "/text", "The text has more than two sentences."};
        data.controls.push_back(std::move(entry));
    }

    const auto tours = json.find("tours");
    if (tours == json.end() || !tours->is_array()) throw TourProblem{"/tours", "The value \"tours\" must be a list."};
    std::set<std::string> seen;
    for (std::size_t i = 0; i < tours->size(); ++i) {
        const std::string pointer = "/tours/" + std::to_string(i);
        const Json& t = (*tours)[i];
        if (!t.is_object()) throw TourProblem{pointer, "Each tour must be a JSON object."};
        TourInfo tour;
        const auto id = t.find("id");
        if (id == t.end() || !id->is_string() || id->get<std::string>().empty()) {
            throw TourProblem{pointer + "/id", "Each tour needs an id."};
        }
        tour.id = id->get<std::string>();
        if (!seen.insert(tour.id).second) throw TourProblem{pointer + "/id", "The tour \"" + tour.id + "\" is listed twice."};
        tour.name = checkedText(t, "name", pointer, kMaxTourNameLength);
        const auto steps = t.find("steps");
        if (steps == t.end() || !steps->is_array() || steps->empty()) {
            throw TourProblem{pointer + "/steps", "Each tour needs at least one step."};
        }
        for (std::size_t s = 0; s < steps->size(); ++s) {
            const std::string sp = pointer + "/steps/" + std::to_string(s);
            const Json& step = (*steps)[s];
            if (!step.is_object()) throw TourProblem{sp, "Each step must be a JSON object."};
            const auto control = step.find("control");
            const auto screen = step.find("screen");
            if (control == step.end() || !control->is_string()) throw TourProblem{sp + "/control", "Each step needs a control."};
            if (screen == step.end() || !screen->is_string()) throw TourProblem{sp + "/screen", "Each step needs a screen."};
            TourStep ts{control->get<std::string>(), screen->get<std::string>()};
            if (std::find(kScreens.begin(), kScreens.end(), ts.screen) == kScreens.end()) {
                throw TourProblem{sp + "/screen", "The screen \"" + ts.screen + "\" is not known."};
            }
            if (!tourEntry(data, ts.control)) {
                throw TourProblem{sp + "/control", "The control \"" + ts.control + "\" has no entry."};
            }
            tour.steps.push_back(std::move(ts));
        }
        data.tours.push_back(std::move(tour));
    }
    return data;
}

}

// Parses tour.json, refusing a repeated control id that a plain JSON parse would hide.
TourRead readTour(std::string_view jsonText)
{
    TourRead out;
    std::string repeated;
    std::set<std::string> keys;
    const Json::parser_callback_t watch = [&](int depth, Json::parse_event_t event, Json& parsed) {
        if (event == Json::parse_event_t::key) {
            const std::string key = parsed.get<std::string>();
            if (depth == 1) {
                keys.clear();
            } else if (depth == 2 && !keys.insert(key).second && repeated.empty()) {
                repeated = key;
            }
        }
        return true;
    };
    const Json json = Json::parse(jsonText.begin(), jsonText.end(), watch, false);
    if (json.is_discarded()) {
        out.status = fail(ErrorCode::BadFile, "The tour file is not valid JSON.");
        return out;
    }
    try {
        if (!json.is_object()) throw TourProblem{"", "The tour file must be a JSON object."};
        const auto format = json.find("format");
        if (format == json.end() || !format->is_string() || format->get<std::string>() != kTourFormat) {
            throw TourProblem{"/format", "The file is not a Plynth tour file."};
        }
        const auto version = json.find("version");
        if (version == json.end() || !version->is_number_integer()) {
            throw TourProblem{"/version", "The value \"version\" must be a whole number."};
        }
        if (version->get<long long>() > kTourVersion) {
            out.status = fail(ErrorCode::UnsupportedVersion, "The tour file was written by a newer Plynth.", "/version");
            return out;
        }
        if (version->get<long long>() < 1) throw TourProblem{"/version", "The version must be 1 or more."};
        if (!repeated.empty()) {
            throw TourProblem{"/controls/" + pointerKey(repeated), "The control \"" + repeated + "\" is listed twice."};
        }
        out.data = readData(json);
    } catch (const TourProblem& problem) {
        out.status = fail(ErrorCode::BadFile, problem.message, problem.where);
        out.data = {};
    }
    return out;
}

// Finds the exact entry, else the longest wildcard that covers the id.
const TourEntry* tourEntry(const TourData& data, std::string_view controlId)
{
    const TourEntry* best = nullptr;
    for (const TourEntry& entry : data.controls) {
        if (entry.id == controlId) return &entry;
        if (wildcardCovers(entry.id, controlId) && (!best || entry.id.size() > best->id.size())) best = &entry;
    }
    return best;
}

// Finds a tour by id.
const TourInfo* findTour(const TourData& data, std::string_view tourId)
{
    for (const TourInfo& tour : data.tours) {
        if (tour.id == tourId) return &tour;
    }
    return nullptr;
}

// The Help menu order of the tours.
std::span<const std::string_view> tourIds()
{
    return kTourIds;
}

// The screens a step may name.
std::span<const std::string_view> tourScreens()
{
    return kScreens;
}

// Walks from one step to the next present one in the given direction.
std::optional<std::size_t> tourStep(const TourInfo& tour, std::optional<std::size_t> from, int direction,
                                    const std::function<bool(const TourStep&)>& present)
{
    const std::size_t count = tour.steps.size();
    if (count == 0 || (direction != 1 && direction != -1)) return std::nullopt;
    const auto shown = [&](std::size_t i) { return !present || present(tour.steps[i]); };
    if (direction == 1) {
        const std::size_t start = from ? *from + 1 : 0;
        for (std::size_t i = start; i < count; ++i) {
            if (shown(i)) return i;
        }
        return std::nullopt;
    }
    if (from && *from == 0) return std::nullopt;
    std::size_t i = from ? std::min(*from, count) : count;
    while (i > 0) {
        --i;
        if (shown(i)) return i;
    }
    return std::nullopt;
}

// Lists ids without entries, entries no control uses and dialogs the audit did not open.
TourCheck checkTourEntries(const TourData& data, std::span<const std::string> controlIds,
                           std::size_t controlsWithoutId, std::span<const std::string> dialogsExpected,
                           std::span<const std::string> dialogsOpened)
{
    TourCheck check;
    check.withoutId = controlsWithoutId;
    std::set<std::string> missing;
    std::set<const TourEntry*> used;
    for (const std::string& id : controlIds) {
        const TourEntry* entry = tourEntry(data, id);
        if (entry) used.insert(entry);
        else missing.insert(id);
    }
    check.withoutEntry.assign(missing.begin(), missing.end());
    for (const TourEntry& entry : data.controls) {
        if (!used.contains(&entry)) check.unused.push_back(entry.id);
    }
    std::sort(check.unused.begin(), check.unused.end());
    const std::set<std::string> opened(dialogsOpened.begin(), dialogsOpened.end());
    std::set<std::string> notOpened;
    for (const std::string& dialog : dialogsExpected) {
        if (!opened.contains(dialog)) notOpened.insert(dialog);
    }
    check.dialogsNotOpened.assign(notOpened.begin(), notOpened.end());
    return check;
}

}
