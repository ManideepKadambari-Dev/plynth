#include "plynth/core/TrimProfiles.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <mutex>
#include <sstream>
#include <system_error>

namespace plynth {

namespace {

// The registry: every profile ever registered. A function local static, so registering from namespace scope
// objects works in any start up order.
struct Registry {
    std::mutex mutex;
    std::vector<std::unique_ptr<TrimProfile>> profiles;
};

// The one registry.
Registry& registry()
{
    static Registry instance;
    return instance;
}

// Says whether text is a well formed profile id: lower case letters, digits and "_", starting with a letter.
bool wellFormedProfileId(std::string_view id)
{
    if (id.empty() || id.size() > 64 || id.front() < 'a' || id.front() > 'z') return false;
    for (char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

// A profile read from a file: a unit box outline stretched to the trim's depth and height.
class OutlineProfile final : public TrimProfile {
public:
    OutlineProfile(std::string id, std::string name, std::vector<Vec2> outline)
        : id_(std::move(id)), name_(std::move(name)), outline_(std::move(outline))
    {
    }

    std::string_view id() const override { return id_; }
    std::string_view name() const override { return name_; }
    int sortKey() const override { return kCustomProfileSortKey; }
    bool builtIn() const override { return false; }

    std::vector<Vec2> outline(double height, double depth) const override
    {
        std::vector<Vec2> out;
        out.reserve(outline_.size());
        for (const Vec2& p : outline_) out.push_back({p.x * depth, p.y * height});
        return out;
    }

private:
    std::string id_;
    std::string name_;
    std::vector<Vec2> outline_;
};

// The cross product of the vectors a to b and a to c.
double cross(Vec2 a, Vec2 b, Vec2 c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// The sign of a cross product, 0 when it is too small to tell.
int sideOf(double value)
{
    constexpr double tiny = 1e-12;
    return value > tiny ? 1 : (value < -tiny ? -1 : 0);
}

// Says whether c, known to be on the line through a and b, lies between them.
bool within(Vec2 a, Vec2 b, Vec2 c)
{
    return std::min(a.x, b.x) - 1e-12 <= c.x && c.x <= std::max(a.x, b.x) + 1e-12 && std::min(a.y, b.y) - 1e-12 <= c.y
        && c.y <= std::max(a.y, b.y) + 1e-12;
}

// Says whether two segments cross or touch.
bool segmentsMeet(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
{
    const int d1 = sideOf(cross(a, b, c));
    const int d2 = sideOf(cross(a, b, d));
    const int d3 = sideOf(cross(c, d, a));
    const int d4 = sideOf(cross(c, d, b));
    if (d1 * d2 < 0 && d3 * d4 < 0) return true;
    if (d1 == 0 && within(a, b, c)) return true;
    if (d2 == 0 && within(a, b, d)) return true;
    if (d3 == 0 && within(c, d, a)) return true;
    if (d4 == 0 && within(c, d, b)) return true;
    return false;
}

// Says whether any two edges of a closed polygon cross or touch, other than neighbours at their shared corner,
// or two neighbours fold back over each other.
bool hasCrossingEdges(const std::vector<Vec2>& p)
{
    const std::size_t n = p.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = p[i];
        const Vec2 b = p[(i + 1) % n];
        for (std::size_t j = i + 1; j < n; ++j) {
            const Vec2 c = p[j];
            const Vec2 d = p[(j + 1) % n];
            const bool neighbours = j == i + 1 || (i == 0 && j == n - 1);
            if (neighbours) {
                const Vec2 shared = (j == i + 1) ? b : a;
                const Vec2 other1 = (j == i + 1) ? a : b;
                const Vec2 other2 = (j == i + 1) ? d : c;
                if (sideOf(cross(shared, other1, other2)) == 0) {
                    const double dot = (other1.x - shared.x) * (other2.x - shared.x) + (other1.y - shared.y) * (other2.y - shared.y);
                    if (dot > 0.0) return true;
                }
                continue;
            }
            if (segmentsMeet(a, b, c, d)) return true;
        }
    }
    return false;
}

// Counts the characters of UTF-8 text, not its bytes.
std::size_t characterCount(const std::string& text)
{
    std::size_t count = 0;
    for (unsigned char c : text) {
        if ((c & 0xC0) != 0x80) ++count;
    }
    return count;
}

// Builds a refused result.
TrimProfileFile refused(const std::string& message, const std::string& where)
{
    TrimProfileFile r;
    r.status = fail(ErrorCode::BadFile, message, where);
    return r;
}

}

// Adds a profile unless its id is taken or malformed.
bool registerTrimProfile(std::unique_ptr<TrimProfile> profile)
{
    if (!profile || !wellFormedProfileId(profile->id())) return false;
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    for (const auto& existing : r.profiles) {
        if (existing->id() == profile->id()) return false;
    }
    r.profiles.push_back(std::move(profile));
    return true;
}

TrimProfileRegistrar::TrimProfileRegistrar(std::unique_ptr<TrimProfile> profile)
{
    registerTrimProfile(std::move(profile));
}

// Finds a profile by id.
const TrimProfile* findTrimProfile(std::string_view id)
{
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    for (const auto& profile : r.profiles) {
        if (profile->id() == id) return profile.get();
    }
    return nullptr;
}

// Every profile, by sort key and then id.
std::vector<const TrimProfile*> allTrimProfiles()
{
    Registry& r = registry();
    std::vector<const TrimProfile*> out;
    {
        std::lock_guard lock(r.mutex);
        for (const auto& profile : r.profiles) out.push_back(profile.get());
    }
    std::sort(out.begin(), out.end(), [](const TrimProfile* a, const TrimProfile* b) {
        if (a->sortKey() != b->sortKey()) return a->sortKey() < b->sortKey();
        return a->id() < b->id();
    });
    return out;
}

// Reads and checks a profile file.
TrimProfileFile readTrimProfileFile(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        TrimProfileFile r;
        r.status = fail(ErrorCode::CannotRead, "The profile file " + file.filename().string() + " could not be opened.");
        return r;
    }
    std::ostringstream text;
    text << in.rdbuf();

    const nlohmann::json json = nlohmann::json::parse(text.str(), nullptr, false);
    if (json.is_discarded()) return refused("The profile file is not valid JSON.", "");
    if (!json.is_object()) return refused("The profile file must hold one JSON object.", "");
    for (auto it = json.begin(); it != json.end(); ++it) {
        bool known = false;
        for (const char* key : {"format", "version", "id", "name", "outline"}) known = known || it.key() == key;
        if (!known) return refused("The field \"" + it.key() + "\" is not part of the format.", "/" + it.key());
    }

    auto format = json.find("format");
    if (format == json.end() || !format->is_string() || format->get<std::string>() != kTrimProfileFormatName) {
        return refused("This is not a " + std::string(kTrimProfileFormatName) + " file.", "/format");
    }
    auto version = json.find("version");
    if (version == json.end() || !version->is_number_integer() || version->get<long long>() != kTrimProfileFormatVersion) {
        return refused("The profile file version must be " + std::to_string(kTrimProfileFormatVersion) + ".", "/version");
    }
    auto id = json.find("id");
    if (id == json.end() || !id->is_string()) return refused("The field \"id\" is missing or not text.", "/id");
    if (!wellFormedProfileId(id->get<std::string>())) {
        return refused("The profile id must use lower case letters, digits and \"_\" and start with a letter.", "/id");
    }
    auto name = json.find("name");
    if (name == json.end() || !name->is_string()) return refused("The field \"name\" is missing or not text.", "/name");
    if (name->get<std::string>().empty() || characterCount(name->get<std::string>()) > 64) {
        return refused("The profile name must be 1 to 64 characters.", "/name");
    }
    auto outline = json.find("outline");
    if (outline == json.end() || !outline->is_array()) return refused("The field \"outline\" is missing or not a list.", "/outline");
    if (outline->size() < 3 || outline->size() > 1000) return refused("The outline needs 3 to 1000 points.", "/outline");

    std::vector<Vec2> points;
    for (std::size_t i = 0; i < outline->size(); ++i) {
        const std::string at = "/outline/" + std::to_string(i);
        const nlohmann::json& pt = (*outline)[i];
        if (!pt.is_array() || pt.size() != 2) return refused("A point must be a list of two numbers.", at);
        for (std::size_t k = 0; k < 2; ++k) {
            const nlohmann::json& v = pt[k];
            if (!v.is_number()) return refused("This value must be a number.", at + "/" + std::to_string(k));
            const double d = v.get<double>();
            if (!std::isfinite(d) || d < 0.0 || d > 1.0) {
                return refused("Outline numbers must be from 0 to 1.", at + "/" + std::to_string(k));
            }
        }
        points.push_back({pt[0].get<double>(), pt[1].get<double>()});
    }

    double minX = 1.0, maxX = 0.0, minY = 1.0, maxY = 0.0;
    for (const Vec2& p : points) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    constexpr double edge = 1e-6;
    if (minX > edge || maxX < 1.0 - edge || minY > edge || maxY < 1.0 - edge) {
        return refused("The outline must touch all four sides of the unit box.", "/outline");
    }
    std::vector<Vec2> distinct;
    for (const Vec2& p : points) {
        if (distinct.empty() || !(distinct.back() == p)) distinct.push_back(p);
    }
    while (distinct.size() > 1 && distinct.front() == distinct.back()) distinct.pop_back();
    points = std::move(distinct);
    if (points.size() < 3) return refused("The outline has no area.", "/outline");
    if (hasCrossingEdges(points)) return refused("The outline crosses itself.", "/outline");
    const double area = signedArea(points);
    if (std::fabs(area) < 1e-9) return refused("The outline has no area.", "/outline");
    // The box has y pointing up, where a positive area means anticlockwise. A negative one is reversed.
    if (area < 0.0) std::reverse(points.begin(), points.end());

    TrimProfileFile result;
    result.id = id->get<std::string>();
    result.name = name->get<std::string>();
    result.unitOutline = std::move(points);
    return result;
}

// Makes a registry entry from a unit box outline.
std::unique_ptr<TrimProfile> makeOutlineProfile(std::string id, std::string name, std::vector<Vec2> unitOutline)
{
    return std::make_unique<OutlineProfile>(std::move(id), std::move(name), std::move(unitOutline));
}

// Registers every profile file in a folder.
TrimProfileFolderLoad registerTrimProfileFolder(const std::filesystem::path& folder)
{
    TrimProfileFolderLoad load;
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) return load;

    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc)) continue;
        const std::string name = it->path().filename().string();
        const std::string_view suffix = kTrimProfileFileSuffix;
        if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
        return a.filename().string() < b.filename().string();
    });

    for (const auto& file : files) {
        const std::string fileName = file.filename().string();
        TrimProfileFile profile = readTrimProfileFile(file);
        if (!profile.status.ok()) {
            load.refused.emplace_back(fileName, profile.status);
            continue;
        }
        const std::string id = profile.id;
        if (!registerTrimProfile(makeOutlineProfile(profile.id, profile.name, std::move(profile.unitOutline)))) {
            load.refused.emplace_back(fileName, fail(ErrorCode::AlreadyExists, "The profile id \"" + id + "\" is already registered."));
            continue;
        }
        load.registered.push_back(id);
    }
    return load;
}

}
