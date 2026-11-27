#include "plynth/core/ProjectJson.h"

#include "NewerFile.h"

#include "plynth/core/MaterialDefaults.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace plynth {

namespace {

// Thrown inside this file when the JSON breaks a rule. Caught at the reader boundary.
struct ReadError {
    Status status;
};

// Stops reading with a BadFile error at a JSON pointer.
[[noreturn]] void refuse(const std::string& message, const std::string& where)
{
    throw ReadError{fail(ErrorCode::BadFile, message, where)};
}

// The JSON pointer of a key inside an object at a pointer.
std::string child(const std::string& where, const std::string& key)
{
    return where + "/" + key;
}

// Requires an object and refuses keys that are not in the allowed list.
const Json& expectObject(const Json& j, const std::string& where, std::initializer_list<const char*> allowed)
{
    if (!j.is_object()) refuse("This value must be an object.", where);
    for (auto it = j.begin(); it != j.end(); ++it) {
        bool known = false;
        for (const char* key : allowed) known = known || it.key() == key;
        if (!known) refuse("The field \"" + it.key() + "\" is not part of the format.", child(where, it.key()));
    }
    return j;
}

// Reads a required string.
std::string getString(const Json& obj, const char* key, const std::string& where)
{
    auto it = obj.find(key);
    if (it == obj.end()) refuse(std::string("The field \"") + key + "\" is missing.", child(where, key));
    if (!it->is_string()) refuse("This value must be text.", child(where, key));
    return it->get<std::string>();
}

// Reads a string that may be missing or null.
std::optional<std::string> getOptionalString(const Json& obj, const char* key, const std::string& where)
{
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return std::nullopt;
    if (!it->is_string()) refuse("This value must be text or null.", child(where, key));
    return it->get<std::string>();
}

// Reads a number that is present. null is refused because a non finite number is written as null.
double numberAt(const Json& value, const std::string& where)
{
    if (!value.is_number()) refuse("This value must be a number.", where);
    const double d = value.get<double>();
    if (!std::isfinite(d)) refuse("This number is not finite.", where);
    return d;
}

// Reads a required number.
double getNumber(const Json& obj, const char* key, const std::string& where)
{
    auto it = obj.find(key);
    if (it == obj.end()) refuse(std::string("The field \"") + key + "\" is missing.", child(where, key));
    return numberAt(*it, child(where, key));
}

// Reads a number that may be missing, which gives the default.
double getNumberOr(const Json& obj, const char* key, const std::string& where, double fallback)
{
    auto it = obj.find(key);
    if (it == obj.end()) return fallback;
    return numberAt(*it, child(where, key));
}

// Reads a number that may be missing or null.
std::optional<double> getOptionalNumber(const Json& obj, const char* key, const std::string& where)
{
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return std::nullopt;
    return numberAt(*it, child(where, key));
}

// Reads a true or false value that may be missing, which gives the default.
bool getBoolOr(const Json& obj, const char* key, const std::string& where, bool fallback)
{
    auto it = obj.find(key);
    if (it == obj.end()) return fallback;
    if (!it->is_boolean()) refuse("This value must be true or false.", child(where, key));
    return it->get<bool>();
}

// Reads an array, which must be present when required.
const Json* getArray(const Json& obj, const char* key, const std::string& where, bool required)
{
    auto it = obj.find(key);
    if (it == obj.end()) {
        if (required) refuse(std::string("The field \"") + key + "\" is missing.", child(where, key));
        return nullptr;
    }
    if (!it->is_array()) refuse("This value must be a list.", child(where, key));
    return &*it;
}

// Reads every item of an optional array with a reader function.
template <typename Item, typename Reader>
std::vector<Item> readList(const Json& obj, const char* key, const std::string& where, Reader reader)
{
    std::vector<Item> items;
    const Json* array = getArray(obj, key, where, false);
    if (!array) return items;
    for (std::size_t i = 0; i < array->size(); ++i) {
        items.push_back(reader((*array)[i], child(child(where, key), std::to_string(i))));
    }
    return items;
}

// Reads one position on the plan.
PlanXY readXY(const Json& j, const std::string& where)
{
    expectObject(j, where, {"x", "y"});
    return PlanXY{getNumber(j, "x", where), getNumber(j, "y", where)};
}

// Reads a list of positions, which must be present when required.
std::vector<PlanXY> readXYList(const Json& obj, const char* key, const std::string& where, bool required)
{
    if (required) getArray(obj, key, where, true);
    return readList<PlanXY>(obj, key, where, readXY);
}

// Reads one cladding zone of a wall.
CladdingZone readCladding(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "side", "start", "end", "bottom", "top", "depth", "material"});
    CladdingZone c;
    c.id = getString(j, "id", where);
    const auto side = wallSideFromName(getString(j, "side", where));
    if (!side) refuse("The side must be \"left\" or \"right\".", child(where, "side"));
    c.side = *side;
    c.start = getNumber(j, "start", where);
    c.end = getNumber(j, "end", where);
    c.bottom = getNumber(j, "bottom", where);
    c.top = getNumber(j, "top", where);
    c.depth = getNumberOr(j, "depth", where, kDefaultCladdingDepth);
    c.material = getString(j, "material", where);
    return c;
}

// Reads one point.
Point readPoint(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "x", "y", "corner_radius"});
    Point p;
    p.id = getString(j, "id", where);
    p.x = getNumber(j, "x", where);
    p.y = getNumber(j, "y", where);
    p.cornerRadius = getNumberOr(j, "corner_radius", where, 0.0);
    return p;
}

// Reads one wall.
Wall readWall(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "from", "to", "thickness", "height", "bulge", "material_inside", "material_outside", "cladding"});
    Wall w;
    w.id = getString(j, "id", where);
    w.from = getString(j, "from", where);
    w.to = getString(j, "to", where);
    w.thickness = getNumberOr(j, "thickness", where, kDefaultWallThickness);
    w.height = getOptionalNumber(j, "height", where);
    w.bulge = getNumberOr(j, "bulge", where, 0.0);
    w.materialInside = getOptionalString(j, "material_inside", where);
    w.materialOutside = getOptionalString(j, "material_outside", where);
    w.cladding = readList<CladdingZone>(j, "cladding", where, readCladding);
    return w;
}

// Reads one room.
Room readRoom(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "type", "walls", "floor_material", "wall_material", "ceiling_material", "dressing"});
    Room r;
    r.id = getString(j, "id", where);
    r.type = getOptionalString(j, "type", where);
    const Json* walls = getArray(j, "walls", where, true);
    for (std::size_t i = 0; i < walls->size(); ++i) {
        const std::string at = child(child(where, "walls"), std::to_string(i));
        if (!(*walls)[i].is_string()) refuse("This value must be text.", at);
        r.walls.push_back((*walls)[i].get<std::string>());
    }
    r.floorMaterial = getOptionalString(j, "floor_material", where);
    r.wallMaterial = getOptionalString(j, "wall_material", where);
    r.ceilingMaterial = getOptionalString(j, "ceiling_material", where);
    r.dressing = getBoolOr(j, "dressing", where, true);
    return r;
}

// Reads one opening.
Opening readOpening(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "wall", "offset", "type", "shape", "width", "height", "sill", "hinge", "swing", "frame",
                            "auto_frame", "fill"});
    Opening o;
    o.id = getString(j, "id", where);
    o.wall = getString(j, "wall", where);
    o.offset = getNumber(j, "offset", where);
    const auto type = openingTypeFromName(getString(j, "type", where));
    if (!type) refuse("The opening type must be \"door\", \"window\" or \"plain\".", child(where, "type"));
    o.type = *type;
    if (j.contains("shape")) o.shape = getString(j, "shape", where);
    o.width = getNumber(j, "width", where);
    o.height = getNumber(j, "height", where);
    o.sill = getNumberOr(j, "sill", where, 0.0);
    if (j.contains("hinge")) {
        const auto hinge = hingeEndFromName(getString(j, "hinge", where));
        if (!hinge) refuse("The hinge must be \"start\" or \"end\".", child(where, "hinge"));
        o.hinge = *hinge;
    }
    if (j.contains("swing")) {
        const auto swing = wallSideFromName(getString(j, "swing", where));
        if (!swing) refuse("The swing must be \"left\" or \"right\".", child(where, "swing"));
        o.swing = *swing;
    }
    o.frame = getOptionalString(j, "frame", where);
    o.autoFrame = getBoolOr(j, "auto_frame", where, true);
    o.fill = getOptionalString(j, "fill", where);
    return o;
}

// Reads the generator of an item.
GeneratorUse readGeneratorUse(const Json& j, const std::string& where)
{
    expectObject(j, where, {"name", "params"});
    GeneratorUse g;
    g.name = getString(j, "name", where);
    auto pit = j.find("params");
    if (pit == j.end()) refuse("The field \"params\" is missing.", child(where, "params"));
    const std::string at = child(where, "params");
    if (!pit->is_object()) refuse("This value must be an object.", at);
    for (auto it = pit->begin(); it != pit->end(); ++it) {
        const std::string vat = child(at, it.key());
        GeneratorParamValue v;
        v.id = it.key();
        if (it->is_number()) v.value = GeneratorValue{numberAt(*it, vat)};
        else if (it->is_boolean()) v.value = GeneratorValue{it->get<bool>()};
        else if (it->is_string()) v.value = GeneratorValue{it->get<std::string>()};
        else if (it->is_null()) v.value = GeneratorValue{std::string()};
        else refuse("This value must be a number, true or false, text or null.", vat);
        g.params.push_back(std::move(v));
    }
    std::sort(g.params.begin(), g.params.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return g;
}

// Reads one placed item.
Item readItem(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "asset", "room", "x", "y", "z", "rotation", "wall", "size", "generator", "on"});
    Item it;
    it.id = getString(j, "id", where);
    if (!j.contains("asset")) refuse("The field \"asset\" is missing.", child(where, "asset"));
    it.asset = getOptionalString(j, "asset", where).value_or("");
    if (auto git = j.find("generator"); git != j.end() && !git->is_null()) it.generator = readGeneratorUse(*git, child(where, "generator"));
    if (it.generator && !it.asset.empty()) refuse("An item has an asset or a generator, not both.", child(where, "generator"));
    if (!it.generator && it.asset.empty()) refuse("An item needs an asset or a generator.", child(where, "asset"));
    it.room = getOptionalString(j, "room", where);
    it.x = getNumber(j, "x", where);
    it.y = getNumber(j, "y", where);
    it.z = getNumberOr(j, "z", where, 0.0);
    it.rotation = getNumberOr(j, "rotation", where, 0.0);
    it.wall = getOptionalString(j, "wall", where);
    auto sit = j.find("size");
    if (sit == j.end()) refuse("The field \"size\" is missing.", child(where, "size"));
    const std::string at = child(where, "size");
    expectObject(*sit, at, {"width", "depth", "height"});
    it.size.width = getNumber(*sit, "width", at);
    it.size.depth = getNumber(*sit, "depth", at);
    it.size.height = getNumber(*sit, "height", at);
    it.on = getOptionalString(j, "on", where);
    return it;
}

// Reads one stair.
Stair readStair(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "type", "x", "y", "rotation", "width", "turn", "material", "rise", "cheeks", "railing"});
    Stair st;
    st.id = getString(j, "id", where);
    if (j.contains("type")) st.type = getString(j, "type", where);
    st.x = getNumber(j, "x", where);
    st.y = getNumber(j, "y", where);
    st.rotation = getNumberOr(j, "rotation", where, 0.0);
    st.width = getNumberOr(j, "width", where, kDefaultStairWidth);
    if (j.contains("turn")) {
        const auto turn = stairTurnFromName(getString(j, "turn", where));
        if (!turn) refuse("The turn must be \"left\" or \"right\".", child(where, "turn"));
        st.turn = *turn;
    }
    st.material = getOptionalString(j, "material", where);
    st.rise = getOptionalNumber(j, "rise", where);
    st.cheeks = getBoolOr(j, "cheeks", where, false);
    st.railing = getBoolOr(j, "railing", where, true);
    return st;
}

// Reads one light marker.
LightMarker readLight(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "kind", "room", "x", "y", "z"});
    LightMarker l;
    l.id = getString(j, "id", where);
    if (j.contains("kind")) {
        const auto kind = lightKindFromName(getString(j, "kind", where));
        if (!kind) refuse("The light kind must be \"ceiling\", \"wall\" or \"standing\".", child(where, "kind"));
        l.kind = *kind;
    }
    l.room = getOptionalString(j, "room", where);
    l.x = getNumber(j, "x", where);
    l.y = getNumber(j, "y", where);
    l.z = getNumber(j, "z", where);
    return l;
}

// Reads one set of roof settings.
RoofSettings readRoofSettings(const Json& j, const std::string& where)
{
    expectObject(j, where, {"type", "pitch", "overhang", "parapet", "direction", "second_pitch", "break_ratio", "ridge_offset", "rise"});
    RoofSettings r;
    r.type = getString(j, "type", where);
    r.pitch = getNumberOr(j, "pitch", where, r.pitch);
    r.overhang = getNumberOr(j, "overhang", where, r.overhang);
    r.parapet = getNumberOr(j, "parapet", where, r.parapet);
    r.direction = getOptionalNumber(j, "direction", where);
    r.secondPitch = getNumberOr(j, "second_pitch", where, r.secondPitch);
    r.breakRatio = getNumberOr(j, "break_ratio", where, r.breakRatio);
    r.ridgeOffset = getNumberOr(j, "ridge_offset", where, r.ridgeOffset);
    r.rise = getNumberOr(j, "rise", where, r.rise);
    return r;
}

// Reads one roof section entry.
RoofSection readRoofSection(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "floor", "x", "y", "settings", "material"});
    RoofSection r;
    r.id = getString(j, "id", where);
    r.floor = getString(j, "floor", where);
    r.x = getNumber(j, "x", where);
    r.y = getNumber(j, "y", where);
    auto sit = j.find("settings");
    if (sit == j.end()) refuse("The field \"settings\" is missing.", child(where, "settings"));
    r.settings = readRoofSettings(*sit, child(where, "settings"));
    r.material = getOptionalString(j, "material", where);
    return r;
}

// Turns roof settings into their JSON form.
Json roofSettingsToJson(const RoofSettings& r)
{
    Json out = Json::object();
    out["type"] = r.type;
    out["pitch"] = r.pitch;
    out["overhang"] = r.overhang;
    out["parapet"] = r.parapet;
    out["direction"] = r.direction ? Json(*r.direction) : Json(nullptr);
    out["second_pitch"] = r.secondPitch;
    out["break_ratio"] = r.breakRatio;
    out["ridge_offset"] = r.ridgeOffset;
    out["rise"] = r.rise;
    return out;
}

// Reads one deck.
Deck readDeck(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "outline", "top", "thickness", "material", "skirt", "skirt_material"});
    Deck d;
    d.id = getString(j, "id", where);
    d.outline = readXYList(j, "outline", where, true);
    d.top = getNumberOr(j, "top", where, d.top);
    d.thickness = getNumberOr(j, "thickness", where, d.thickness);
    d.material = getOptionalString(j, "material", where);
    d.skirt = getBoolOr(j, "skirt", where, d.skirt);
    d.skirtMaterial = getOptionalString(j, "skirt_material", where);
    return d;
}

// Reads one railing.
Railing readRailing(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "points", "closed", "host", "base", "height", "style", "material", "glass_material"});
    Railing r;
    r.id = getString(j, "id", where);
    r.points = readXYList(j, "points", where, true);
    r.closed = getBoolOr(j, "closed", where, r.closed);
    r.host = getOptionalString(j, "host", where);
    r.base = getNumberOr(j, "base", where, r.base);
    r.height = getNumberOr(j, "height", where, r.height);
    if (j.contains("style")) r.style = getString(j, "style", where);
    r.material = getOptionalString(j, "material", where);
    r.glassMaterial = getOptionalString(j, "glass_material", where);
    return r;
}

// Reads one canopy.
Canopy readCanopy(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "outline", "height", "thickness", "roof", "material", "underside_material", "fascia_material"});
    Canopy c;
    c.id = getString(j, "id", where);
    c.outline = readXYList(j, "outline", where, true);
    c.height = getNumberOr(j, "height", where, c.height);
    c.thickness = getNumberOr(j, "thickness", where, c.thickness);
    if (auto rit = j.find("roof"); rit != j.end()) c.roof = readRoofSettings(*rit, child(where, "roof"));
    c.material = getOptionalString(j, "material", where);
    c.undersideMaterial = getOptionalString(j, "underside_material", where);
    c.fasciaMaterial = getOptionalString(j, "fascia_material", where);
    return c;
}

// Reads one dormer.
Dormer readDormer(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "x", "y", "type", "width", "depth", "pitch", "window_width", "window_height", "window_sill",
                            "material", "roof_material"});
    Dormer d;
    d.id = getString(j, "id", where);
    d.x = getNumber(j, "x", where);
    d.y = getNumber(j, "y", where);
    if (j.contains("type")) d.type = getString(j, "type", where);
    d.width = getNumberOr(j, "width", where, d.width);
    d.depth = getNumberOr(j, "depth", where, d.depth);
    d.pitch = getNumberOr(j, "pitch", where, d.pitch);
    d.windowWidth = getNumberOr(j, "window_width", where, d.windowWidth);
    d.windowHeight = getNumberOr(j, "window_height", where, d.windowHeight);
    d.windowSill = getNumberOr(j, "window_sill", where, d.windowSill);
    d.material = getOptionalString(j, "material", where);
    d.roofMaterial = getOptionalString(j, "roof_material", where);
    return d;
}

// Reads one light strip.
LightStrip readLightStrip(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "kind", "points", "z", "stair", "power"});
    LightStrip l;
    l.id = getString(j, "id", where);
    const auto kind = lightStripKindFromName(getString(j, "kind", where));
    if (!kind) refuse("The light strip kind must be \"cove\", \"soffit\", \"edge\" or \"step\".", child(where, "kind"));
    l.kind = *kind;
    l.points = readXYList(j, "points", where, false);
    l.z = getNumberOr(j, "z", where, l.z);
    l.stair = getOptionalString(j, "stair", where);
    l.power = getNumberOr(j, "power", where, l.power);
    return l;
}

// Reads one gate of a site boundary.
Gate readGate(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "offset", "width", "sliding", "material"});
    Gate g;
    g.id = getString(j, "id", where);
    g.offset = getNumber(j, "offset", where);
    g.width = getNumberOr(j, "width", where, g.width);
    g.sliding = getBoolOr(j, "sliding", where, g.sliding);
    g.material = getOptionalString(j, "material", where);
    return g;
}

// Reads one fence or boundary wall.
SiteBoundary readBoundary(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "kind", "points", "closed", "height", "thickness", "style", "material", "gates"});
    SiteBoundary b;
    b.id = getString(j, "id", where);
    if (j.contains("kind")) {
        const auto kind = boundaryKindFromName(getString(j, "kind", where));
        if (!kind) refuse("The boundary kind must be \"fence\" or \"wall\".", child(where, "kind"));
        b.kind = *kind;
    }
    b.points = readXYList(j, "points", where, true);
    b.closed = getBoolOr(j, "closed", where, b.closed);
    b.height = getNumberOr(j, "height", where, b.height);
    b.thickness = getNumberOr(j, "thickness", where, b.thickness);
    if (j.contains("style")) b.style = getString(j, "style", where);
    b.material = getOptionalString(j, "material", where);
    b.gates = readList<Gate>(j, "gates", where, readGate);
    return b;
}

// Reads one flat site surface.
SiteSurface readSiteSurface(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "kind", "outline", "thickness", "material"});
    SiteSurface s;
    s.id = getString(j, "id", where);
    if (j.contains("kind")) {
        const auto kind = siteSurfaceKindFromName(getString(j, "kind", where));
        if (!kind) refuse("The surface kind must be \"path\", \"driveway\", \"paving\" or \"lawn\".", child(where, "kind"));
        s.kind = *kind;
    }
    s.outline = readXYList(j, "outline", where, true);
    s.thickness = getNumberOr(j, "thickness", where, s.thickness);
    s.material = getOptionalString(j, "material", where);
    return s;
}

// Reads everything round the building.
Site readSite(const Json& j, const std::string& where)
{
    expectObject(j, where, {"ground", "boundaries", "surfaces"});
    Site site;
    if (auto git = j.find("ground"); git != j.end()) {
        const std::string at = child(where, "ground");
        expectObject(*git, at, {"enabled", "margin", "material"});
        site.ground.enabled = getBoolOr(*git, "enabled", at, site.ground.enabled);
        site.ground.margin = getNumberOr(*git, "margin", at, site.ground.margin);
        site.ground.material = getOptionalString(*git, "material", at);
    }
    site.boundaries = readList<SiteBoundary>(j, "boundaries", where, readBoundary);
    site.surfaces = readList<SiteSurface>(j, "surfaces", where, readSiteSurface);
    return site;
}

// Reads the foundation band.
Foundation readFoundation(const Json& j, const std::string& where)
{
    expectObject(j, where, {"enabled", "height", "step_out", "material"});
    Foundation f;
    f.enabled = getBoolOr(j, "enabled", where, f.enabled);
    f.height = getNumberOr(j, "height", where, f.height);
    f.stepOut = getNumberOr(j, "step_out", where, f.stepOut);
    f.material = getOptionalString(j, "material", where);
    return f;
}

// Writes text that may be empty, as null when it is.
Json optionalText(const std::optional<std::string>& v)
{
    return v ? Json(*v) : Json(nullptr);
}

// Writes a number that may be empty, as null when it is.
Json optionalNumber(const std::optional<double>& v)
{
    return v ? Json(*v) : Json(nullptr);
}

// Writes a list of positions.
Json xyListToJson(const std::vector<PlanXY>& points)
{
    Json out = Json::array();
    for (const PlanXY& p : points) {
        Json jp = Json::object();
        jp["x"] = p.x;
        jp["y"] = p.y;
        out.push_back(std::move(jp));
    }
    return out;
}

// Writes the cladding zones of a wall.
Json claddingToJson(const std::vector<CladdingZone>& zones)
{
    Json out = Json::array();
    for (const CladdingZone& c : zones) {
        Json jc = Json::object();
        jc["id"] = c.id;
        jc["side"] = std::string(wallSideName(c.side));
        jc["start"] = c.start;
        jc["end"] = c.end;
        jc["bottom"] = c.bottom;
        jc["top"] = c.top;
        jc["depth"] = c.depth;
        jc["material"] = c.material;
        out.push_back(std::move(jc));
    }
    return out;
}

// Writes the decks of a floor.
Json decksToJson(const std::vector<Deck>& decks)
{
    Json out = Json::array();
    for (const Deck& d : decks) {
        Json jd = Json::object();
        jd["id"] = d.id;
        jd["outline"] = xyListToJson(d.outline);
        jd["top"] = d.top;
        jd["thickness"] = d.thickness;
        jd["material"] = optionalText(d.material);
        jd["skirt"] = d.skirt;
        jd["skirt_material"] = optionalText(d.skirtMaterial);
        out.push_back(std::move(jd));
    }
    return out;
}

// Writes the railings of a floor.
Json railingsToJson(const std::vector<Railing>& railings)
{
    Json out = Json::array();
    for (const Railing& r : railings) {
        Json jr = Json::object();
        jr["id"] = r.id;
        jr["points"] = xyListToJson(r.points);
        jr["closed"] = r.closed;
        jr["host"] = optionalText(r.host);
        jr["base"] = r.base;
        jr["height"] = r.height;
        jr["style"] = r.style;
        jr["material"] = optionalText(r.material);
        jr["glass_material"] = optionalText(r.glassMaterial);
        out.push_back(std::move(jr));
    }
    return out;
}

// Writes the canopies of a floor.
Json canopiesToJson(const std::vector<Canopy>& canopies)
{
    Json out = Json::array();
    for (const Canopy& c : canopies) {
        Json jc = Json::object();
        jc["id"] = c.id;
        jc["outline"] = xyListToJson(c.outline);
        jc["height"] = c.height;
        jc["thickness"] = c.thickness;
        jc["roof"] = roofSettingsToJson(c.roof);
        jc["material"] = optionalText(c.material);
        jc["underside_material"] = optionalText(c.undersideMaterial);
        jc["fascia_material"] = optionalText(c.fasciaMaterial);
        out.push_back(std::move(jc));
    }
    return out;
}

// Writes the dormers of a floor.
Json dormersToJson(const std::vector<Dormer>& dormers)
{
    Json out = Json::array();
    for (const Dormer& d : dormers) {
        Json jd = Json::object();
        jd["id"] = d.id;
        jd["x"] = d.x;
        jd["y"] = d.y;
        jd["type"] = d.type;
        jd["width"] = d.width;
        jd["depth"] = d.depth;
        jd["pitch"] = d.pitch;
        jd["window_width"] = d.windowWidth;
        jd["window_height"] = d.windowHeight;
        jd["window_sill"] = d.windowSill;
        jd["material"] = optionalText(d.material);
        jd["roof_material"] = optionalText(d.roofMaterial);
        out.push_back(std::move(jd));
    }
    return out;
}

// Writes the light strips of a floor.
Json lightStripsToJson(const std::vector<LightStrip>& strips)
{
    Json out = Json::array();
    for (const LightStrip& l : strips) {
        Json jl = Json::object();
        jl["id"] = l.id;
        jl["kind"] = std::string(lightStripKindName(l.kind));
        jl["points"] = xyListToJson(l.points);
        jl["z"] = l.z;
        jl["stair"] = optionalText(l.stair);
        jl["power"] = l.power;
        out.push_back(std::move(jl));
    }
    return out;
}

// Writes everything round the building.
Json siteToJson(const Site& site)
{
    Json out = Json::object();
    Json ground = Json::object();
    ground["enabled"] = site.ground.enabled;
    ground["margin"] = site.ground.margin;
    ground["material"] = optionalText(site.ground.material);
    out["ground"] = std::move(ground);
    Json boundaries = Json::array();
    for (const SiteBoundary& b : site.boundaries) {
        Json jb = Json::object();
        jb["id"] = b.id;
        jb["kind"] = std::string(boundaryKindName(b.kind));
        jb["points"] = xyListToJson(b.points);
        jb["closed"] = b.closed;
        jb["height"] = b.height;
        jb["thickness"] = b.thickness;
        jb["style"] = b.style;
        jb["material"] = optionalText(b.material);
        Json gates = Json::array();
        for (const Gate& g : b.gates) {
            Json jg = Json::object();
            jg["id"] = g.id;
            jg["offset"] = g.offset;
            jg["width"] = g.width;
            jg["sliding"] = g.sliding;
            jg["material"] = optionalText(g.material);
            gates.push_back(std::move(jg));
        }
        jb["gates"] = std::move(gates);
        boundaries.push_back(std::move(jb));
    }
    out["boundaries"] = std::move(boundaries);
    Json surfaces = Json::array();
    for (const SiteSurface& s : site.surfaces) {
        Json js = Json::object();
        js["id"] = s.id;
        js["kind"] = std::string(siteSurfaceKindName(s.kind));
        js["outline"] = xyListToJson(s.outline);
        js["thickness"] = s.thickness;
        js["material"] = optionalText(s.material);
        surfaces.push_back(std::move(js));
    }
    out["surfaces"] = std::move(surfaces);
    return out;
}

// Writes the foundation band.
Json foundationToJson(const Foundation& f)
{
    Json out = Json::object();
    out["enabled"] = f.enabled;
    out["height"] = f.height;
    out["step_out"] = f.stepOut;
    out["material"] = optionalText(f.material);
    return out;
}

// Reads one floor.
Floor readFloor(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "name", "image", "ceiling_height", "points", "walls", "rooms", "openings", "items",
                         "slab_thickness", "image_offset", "stairs", "lights", "decks", "railings", "canopies", "dormers",
                         "light_strips"});
    Floor f;
    f.id = getString(j, "id", where);
    f.name = j.contains("name") ? getString(j, "name", where) : f.id;
    f.image = getOptionalString(j, "image", where);
    f.ceilingHeight = getNumberOr(j, "ceiling_height", where, kDefaultCeilingHeight);
    f.points = readList<Point>(j, "points", where, readPoint);
    f.walls = readList<Wall>(j, "walls", where, readWall);
    f.rooms = readList<Room>(j, "rooms", where, readRoom);
    f.openings = readList<Opening>(j, "openings", where, readOpening);
    f.items = readList<Item>(j, "items", where, readItem);
    f.slabThickness = getNumberOr(j, "slab_thickness", where, kDefaultSlabThickness);
    if (auto oit = j.find("image_offset"); oit != j.end()) {
        const std::string at = child(where, "image_offset");
        expectObject(*oit, at, {"x", "y"});
        f.imageOffset.x = getNumber(*oit, "x", at);
        f.imageOffset.y = getNumber(*oit, "y", at);
    }
    f.stairs = readList<Stair>(j, "stairs", where, readStair);
    f.lights = readList<LightMarker>(j, "lights", where, readLight);
    f.decks = readList<Deck>(j, "decks", where, readDeck);
    f.railings = readList<Railing>(j, "railings", where, readRailing);
    f.canopies = readList<Canopy>(j, "canopies", where, readCanopy);
    f.dormers = readList<Dormer>(j, "dormers", where, readDormer);
    f.lightStrips = readList<LightStrip>(j, "light_strips", where, readLightStrip);
    return f;
}

// Checks "format" and upgrades the JSON to the current version. found gets the version in the file, even when the upgrade fails.
void prepare(Json& json, std::string_view format, int currentVersion, std::span<const Migration> steps, int& found,
             bool& migrated)
{
    if (!json.is_object()) refuse("The file must hold one JSON object.", "");
    auto fit = json.find("format");
    if (fit == json.end()) refuse("The field \"format\" is missing.", "/format");
    if (!fit->is_string() || fit->get<std::string>() != format) {
        refuse("This is not a " + std::string(format) + " file.", "/format");
    }
    found = 0;
    auto vit = json.find("version");
    if (vit != json.end() && vit->is_number_integer()) found = vit->get<int>();

    if (auto s = migrateJson(json, currentVersion, steps); !s.ok()) {
        if (s.code == ErrorCode::UnsupportedVersion) {
            const std::string_view file = format == kPlanFormatName ? kPlanFileName : kProjectFileName;
            s.message = newerFileMessage(file, json.at("version").get<long long>());
        }
        throw ReadError{s};
    }
    migrated = found != currentVersion;
}

// Writes the generator of an item, parameters sorted by id.
Json generatorUseToJson(const GeneratorUse& g)
{
    Json out = Json::object();
    out["name"] = g.name;
    std::vector<const GeneratorParamValue*> sorted;
    for (const auto& p : g.params) sorted.push_back(&p);
    std::sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b) { return a->id < b->id; });
    Json params = Json::object();
    for (const auto* p : sorted) {
        if (const double* d = std::get_if<double>(&p->value)) params[p->id] = *d;
        else if (const bool* b = std::get_if<bool>(&p->value)) params[p->id] = *b;
        else {
            const std::string& text = std::get<std::string>(p->value);
            params[p->id] = text.empty() ? Json(nullptr) : Json(text);
        }
    }
    out["params"] = std::move(params);
    return out;
}

}

// Turns a plan into its plan.json form.
Json planToJson(const Plan& plan)
{
    Json out = Json::object();
    out["format"] = std::string(kPlanFormatName);
    out["version"] = kPlanFormatVersion;

    Json scale = Json::object();
    scale["pixels_per_metre"] = plan.scale.pixelsPerMetre ? Json(*plan.scale.pixelsPerMetre) : Json(nullptr);
    out["scale"] = std::move(scale);

    const auto optString = optionalText;
    const auto optNumber = optionalNumber;

    Json floors = Json::array();
    for (const Floor& f : plan.floors) {
        Json jf = Json::object();
        jf["id"] = f.id;
        jf["name"] = f.name;
        jf["image"] = optString(f.image);
        jf["ceiling_height"] = f.ceilingHeight;

        Json points = Json::array();
        for (const Point& p : f.points) {
            Json jp = Json::object();
            jp["id"] = p.id;
            jp["x"] = p.x;
            jp["y"] = p.y;
            jp["corner_radius"] = p.cornerRadius;
            points.push_back(std::move(jp));
        }
        jf["points"] = std::move(points);

        Json walls = Json::array();
        for (const Wall& w : f.walls) {
            Json jw = Json::object();
            jw["id"] = w.id;
            jw["from"] = w.from;
            jw["to"] = w.to;
            jw["thickness"] = w.thickness;
            jw["height"] = optNumber(w.height);
            jw["bulge"] = w.bulge;
            jw["material_inside"] = optString(w.materialInside);
            jw["material_outside"] = optString(w.materialOutside);
            jw["cladding"] = claddingToJson(w.cladding);
            walls.push_back(std::move(jw));
        }
        jf["walls"] = std::move(walls);

        Json rooms = Json::array();
        for (const Room& r : f.rooms) {
            Json jr = Json::object();
            jr["id"] = r.id;
            jr["type"] = optString(r.type);
            Json rw = Json::array();
            for (const Id& id : r.walls) rw.push_back(id);
            jr["walls"] = std::move(rw);
            jr["floor_material"] = optString(r.floorMaterial);
            jr["wall_material"] = optString(r.wallMaterial);
            jr["ceiling_material"] = optString(r.ceilingMaterial);
            jr["dressing"] = r.dressing;
            rooms.push_back(std::move(jr));
        }
        jf["rooms"] = std::move(rooms);

        Json openings = Json::array();
        for (const Opening& o : f.openings) {
            Json jo = Json::object();
            jo["id"] = o.id;
            jo["wall"] = o.wall;
            jo["offset"] = o.offset;
            jo["type"] = std::string(openingTypeName(o.type));
            jo["shape"] = o.shape;
            jo["width"] = o.width;
            jo["height"] = o.height;
            jo["sill"] = o.sill;
            jo["hinge"] = std::string(hingeEndName(o.hinge));
            jo["swing"] = std::string(wallSideName(o.swing));
            jo["frame"] = optString(o.frame);
            jo["auto_frame"] = o.autoFrame;
            jo["fill"] = optString(o.fill);
            openings.push_back(std::move(jo));
        }
        jf["openings"] = std::move(openings);

        Json items = Json::array();
        for (const Item& it : f.items) {
            Json ji = Json::object();
            ji["id"] = it.id;
            ji["asset"] = it.asset.empty() ? Json(nullptr) : Json(it.asset);
            ji["room"] = optString(it.room);
            ji["x"] = it.x;
            ji["y"] = it.y;
            ji["z"] = it.z;
            ji["rotation"] = it.rotation;
            ji["wall"] = optString(it.wall);
            Json size = Json::object();
            size["width"] = it.size.width;
            size["depth"] = it.size.depth;
            size["height"] = it.size.height;
            ji["size"] = std::move(size);
            ji["generator"] = it.generator ? generatorUseToJson(*it.generator) : Json(nullptr);
            ji["on"] = optString(it.on);
            items.push_back(std::move(ji));
        }
        jf["items"] = std::move(items);

        jf["slab_thickness"] = f.slabThickness;
        Json offset = Json::object();
        offset["x"] = f.imageOffset.x;
        offset["y"] = f.imageOffset.y;
        jf["image_offset"] = std::move(offset);

        Json stairs = Json::array();
        for (const Stair& st : f.stairs) {
            Json js = Json::object();
            js["id"] = st.id;
            js["type"] = st.type;
            js["x"] = st.x;
            js["y"] = st.y;
            js["rotation"] = st.rotation;
            js["width"] = st.width;
            js["turn"] = std::string(stairTurnName(st.turn));
            js["material"] = optString(st.material);
            js["rise"] = optNumber(st.rise);
            js["cheeks"] = st.cheeks;
            js["railing"] = st.railing;
            stairs.push_back(std::move(js));
        }
        jf["stairs"] = std::move(stairs);

        Json lights = Json::array();
        for (const LightMarker& l : f.lights) {
            Json jl = Json::object();
            jl["id"] = l.id;
            jl["kind"] = std::string(lightKindName(l.kind));
            jl["room"] = optString(l.room);
            jl["x"] = l.x;
            jl["y"] = l.y;
            jl["z"] = l.z;
            lights.push_back(std::move(jl));
        }
        jf["lights"] = std::move(lights);
        jf["decks"] = decksToJson(f.decks);
        jf["railings"] = railingsToJson(f.railings);
        jf["canopies"] = canopiesToJson(f.canopies);
        jf["dormers"] = dormersToJson(f.dormers);
        jf["light_strips"] = lightStripsToJson(f.lightStrips);
        floors.push_back(std::move(jf));
    }
    out["floors"] = std::move(floors);
    out["roof"] = roofSettingsToJson(plan.roof);
    Json sections = Json::array();
    for (const RoofSection& r : plan.roofSections) {
        Json jr = Json::object();
        jr["id"] = r.id;
        jr["floor"] = r.floor;
        jr["x"] = r.x;
        jr["y"] = r.y;
        jr["settings"] = roofSettingsToJson(r.settings);
        jr["material"] = optString(r.material);
        sections.push_back(std::move(jr));
    }
    out["roof_sections"] = std::move(sections);
    out["ground_floor"] = optString(plan.groundFloor);
    out["site"] = siteToJson(plan.site);
    out["foundation"] = foundationToJson(plan.foundation);
    return out;
}

// Reads a plan from its plan.json form.
PlanRead planFromJson(const Json& json)
{
    PlanRead result;
    try {
        Json copy = json;
        prepare(copy, kPlanFormatName, kPlanFormatVersion, planMigrations(), result.fileVersion, result.migrated);
        expectObject(copy, "", {"format", "version", "scale", "floors", "roof", "roof_sections", "ground_floor", "site", "foundation"});

        Plan plan;
        auto sit = copy.find("scale");
        if (sit != copy.end()) {
            expectObject(*sit, "/scale", {"pixels_per_metre"});
            plan.scale.pixelsPerMetre = getOptionalNumber(*sit, "pixels_per_metre", "/scale");
        }
        const Json* floors = getArray(copy, "floors", "", true);
        for (std::size_t i = 0; i < floors->size(); ++i) {
            plan.floors.push_back(readFloor((*floors)[i], "/floors/" + std::to_string(i)));
        }
        if (auto rit = copy.find("roof"); rit != copy.end()) plan.roof = readRoofSettings(*rit, "/roof");
        if (const Json* sections = getArray(copy, "roof_sections", "", false)) {
            for (std::size_t i = 0; i < sections->size(); ++i) {
                plan.roofSections.push_back(readRoofSection((*sections)[i], "/roof_sections/" + std::to_string(i)));
            }
        }
        plan.groundFloor = getOptionalString(copy, "ground_floor", "");
        if (auto siteIt = copy.find("site"); siteIt != copy.end()) plan.site = readSite(*siteIt, "/site");
        if (auto fit = copy.find("foundation"); fit != copy.end()) plan.foundation = readFoundation(*fit, "/foundation");
        if (Status s = checkPlan(plan); !s.ok()) {
            refuse(s.message, s.where);
        }
        result.plan = std::move(plan);
    } catch (const ReadError& e) {
        result.status = e.status;
        result.plan = Plan{};
    } catch (const std::exception& e) {
        result.status = fail(ErrorCode::BadFile, std::string("The plan could not be read: ") + e.what());
        result.plan = Plan{};
    }
    return result;
}

namespace {

// Turns default sizes into their project.json form.
Json defaultsToJson(const DefaultSizes& d)
{
    Json out = Json::object();
    out["ceiling_height"] = d.ceilingHeight;
    out["door_width"] = d.doorWidth;
    out["door_height"] = d.doorHeight;
    out["window_width"] = d.windowWidth;
    out["window_height"] = d.windowHeight;
    out["window_sill"] = d.windowSill;
    out["wall_item_centre"] = d.wallItemCentre;
    return out;
}

// Turns the trim settings into their project.json form.
Json trimToJson(const TrimSettings& trim)
{
    Json out = Json::object();
    for (TrimKind kind : {TrimKind::Skirting, TrimKind::Cornice, TrimKind::Casing, TrimKind::Sill, TrimKind::Railing}) {
        const TrimStyle& style = trimStyle(trim, kind);
        Json js = Json::object();
        js["enabled"] = style.enabled;
        js["profile"] = style.profile;
        js["height"] = style.height;
        js["depth"] = style.depth;
        out[std::string(trimKindName(kind))] = std::move(js);
    }
    out["railing_height"] = trim.railingHeight;
    return out;
}

// Turns the variation settings into their project.json form.
Json variationToJson(const VariationSettings& v)
{
    Json out = Json::object();
    out["seed"] = v.seed;
    out["furniture"] = v.furniture;
    out["materials"] = v.materials;
    return out;
}

// Turns the default materials into their project.json form, every surface written.
Json materialsToJson(const MaterialDefaults& defaults)
{
    Json out = Json::object();
    for (const Surface surface : allSurfaces()) {
        const std::optional<std::string>& value = materialDefault(defaults, surface);
        out[std::string(surfaceName(surface))] = value ? Json(*value) : Json(nullptr);
    }
    return out;
}

}

// Turns project settings into their project.json form.
Json settingsToJson(const ProjectSettings& settings)
{
    Json out = Json::object();
    out["format"] = std::string(kProjectFormatName);
    out["version"] = kProjectFormatVersion;
    out["name"] = settings.name;
    out["export_mode"] = std::string(exportModeName(settings.exportMode));
    out["grid_size"] = settings.gridSize;
    out["defaults"] = defaultsToJson(settings.defaults);
    out["materials"] = materialsToJson(settings.materials);
    out["trim"] = trimToJson(settings.trim);
    out["variation"] = variationToJson(settings.variation);
    return out;
}

// Reads project settings from their project.json form.
SettingsRead settingsFromJson(const Json& json)
{
    SettingsRead result;
    try {
        Json copy = json;
        prepare(copy, kProjectFormatName, kProjectFormatVersion, projectMigrations(), result.fileVersion, result.migrated);
        expectObject(copy, "", {"format", "version", "name", "export_mode", "grid_size", "defaults", "materials", "trim",
                                "variation"});

        ProjectSettings settings;
        settings.name = getString(copy, "name", "");
        if (settings.name.empty()) refuse("The project name is empty.", "/name");
        if (copy.contains("export_mode")) {
            const auto mode = exportModeFromName(getString(copy, "export_mode", ""));
            if (!mode) refuse("The export mode must be \"single\" or \"grouped\".", "/export_mode");
            settings.exportMode = *mode;
        }
        settings.gridSize = getNumberOr(copy, "grid_size", "", kDefaultGridSize);
        if (!(settings.gridSize > 0.0)) refuse("The grid size must be a number above zero.", "/grid_size");
        if (auto dit = copy.find("defaults"); dit != copy.end()) {
            expectObject(*dit, "/defaults",
                         {"ceiling_height", "door_width", "door_height", "window_width", "window_height", "window_sill", "wall_item_centre"});
            DefaultSizes& d = settings.defaults;
            d.ceilingHeight = getNumberOr(*dit, "ceiling_height", "/defaults", d.ceilingHeight);
            d.doorWidth = getNumberOr(*dit, "door_width", "/defaults", d.doorWidth);
            d.doorHeight = getNumberOr(*dit, "door_height", "/defaults", d.doorHeight);
            d.windowWidth = getNumberOr(*dit, "window_width", "/defaults", d.windowWidth);
            d.windowHeight = getNumberOr(*dit, "window_height", "/defaults", d.windowHeight);
            d.windowSill = getNumberOr(*dit, "window_sill", "/defaults", d.windowSill);
            d.wallItemCentre = getNumberOr(*dit, "wall_item_centre", "/defaults", d.wallItemCentre);
            if (Status s = checkDefaultSizes(d); !s.ok()) refuse(s.message, "/defaults");
        }
        if (auto mit = copy.find("materials"); mit != copy.end()) {
            expectObject(*mit, "/materials", {"wall_inside", "wall_outside", "wall_top", "floor", "ceiling", "door", "frame", "glass", "handle",
                          "roof", "flat_roof", "stair", "trim", "railing", "deck", "foundation", "ground", "paving", "fence",
                          "boundary_wall"});
            for (const Surface surface : allSurfaces()) {
                const std::string key(surfaceName(surface));
                if (!mit->contains(key)) continue;
                setMaterialDefault(settings.materials, surface, getOptionalString(*mit, key.c_str(), "/materials"));
            }
            if (Status s = checkMaterialDefaults(settings.materials); !s.ok()) refuse(s.message, s.where);
        }
        if (auto tit = copy.find("trim"); tit != copy.end()) {
            expectObject(*tit, "/trim", {"skirting", "cornice", "casing", "sill", "railing", "railing_height"});
            for (TrimKind kind : {TrimKind::Skirting, TrimKind::Cornice, TrimKind::Casing, TrimKind::Sill, TrimKind::Railing}) {
                const std::string key(trimKindName(kind));
                auto sit = tit->find(key);
                if (sit == tit->end()) continue;
                const std::string at = "/trim/" + key;
                expectObject(*sit, at, {"enabled", "profile", "height", "depth"});
                TrimStyle& style = trimStyle(settings.trim, kind);
                style.enabled = getBoolOr(*sit, "enabled", at, style.enabled);
                if (sit->contains("profile")) style.profile = getString(*sit, "profile", at);
                style.height = getNumberOr(*sit, "height", at, style.height);
                style.depth = getNumberOr(*sit, "depth", at, style.depth);
            }
            settings.trim.railingHeight = getNumberOr(*tit, "railing_height", "/trim", settings.trim.railingHeight);
            if (Status s = checkTrimSettings(settings.trim); !s.ok()) refuse(s.message, "/trim");
        }
        if (auto vit = copy.find("variation"); vit != copy.end()) {
            expectObject(*vit, "/variation", {"seed", "furniture", "materials"});
            if (auto seed = vit->find("seed"); seed != vit->end()) {
                if (!seed->is_number_integer() || seed->get<long long>() < 0 || seed->get<long long>() > 4294967295LL) {
                    refuse("The seed must be a whole number from 0 to 4294967295.", "/variation/seed");
                }
                settings.variation.seed = static_cast<std::uint32_t>(seed->get<long long>());
            }
            settings.variation.furniture = getBoolOr(*vit, "furniture", "/variation", true);
            settings.variation.materials = getBoolOr(*vit, "materials", "/variation", true);
        }
        result.settings = std::move(settings);
    } catch (const ReadError& e) {
        result.status = e.status;
    } catch (const std::exception& e) {
        result.status = fail(ErrorCode::BadFile, std::string("The project settings could not be read: ") + e.what());
    }
    return result;
}

// Runs upgrade steps until the JSON reaches the current version.
Status migrateJson(Json& json, int currentVersion, std::span<const Migration> steps)
{
    if (!json.is_object()) return fail(ErrorCode::BadFile, "The file must hold one JSON object.");
    auto it = json.find("version");
    if (it == json.end()) return fail(ErrorCode::BadFile, "The field \"version\" is missing.", "/version");
    if (!it->is_number_integer()) return fail(ErrorCode::BadFile, "The version must be a whole number.", "/version");
    long long version = it->get<long long>();
    if (version < 1) return fail(ErrorCode::BadFile, "The version must be 1 or more.", "/version");
    if (version > currentVersion) {
        return fail(ErrorCode::UnsupportedVersion,
                    "This file is version " + std::to_string(version) + ", but this Plynth reads up to version "
                        + std::to_string(currentVersion) + ". Update Plynth to open it.",
                    "/version");
    }
    while (version < currentVersion) {
        const Migration* found = nullptr;
        for (const Migration& m : steps) {
            if (m.fromVersion == version) {
                found = &m;
                break;
            }
        }
        if (!found || !found->step) {
            return fail(ErrorCode::BadFile, "There is no upgrade step from version " + std::to_string(version) + ".", "/version");
        }
        if (Status s = found->step(json); !s.ok()) return s;
        ++version;
        json["version"] = version;
    }
    return {};
}

namespace {

// Version 1 to 2: every opening gets a hinge and a swing.
Status migratePlan1To2(Json& json)
{
    auto floors = json.find("floors");
    if (floors == json.end() || !floors->is_array()) return {};
    for (Json& floor : *floors) {
        if (!floor.is_object()) continue;
        auto openings = floor.find("openings");
        if (openings == floor.end() || !openings->is_array()) continue;
        for (Json& opening : *openings) {
            if (!opening.is_object()) continue;
            if (!opening.contains("hinge")) opening["hinge"] = "start";
            if (!opening.contains("swing")) opening["swing"] = "right";
        }
    }
    return {};
}

// Version 1 to 2: the project gets the built-in default sizes.
Status migrateProject1To2(Json& json)
{
    if (!json.is_object()) return {};
    if (!json.contains("defaults")) json["defaults"] = defaultsToJson(DefaultSizes{});
    return {};
}

// Version 2 to 3: every floor gets an empty list of items.
Status migratePlan2To3(Json& json)
{
    auto floors = json.find("floors");
    if (floors == json.end() || !floors->is_array()) return {};
    for (Json& floor : *floors) {
        if (floor.is_object() && !floor.contains("items")) floor["items"] = Json::array();
    }
    return {};
}

// Version 2 to 3: the defaults get the wall item centre.
Status migrateProject2To3(Json& json)
{
    if (!json.is_object()) return {};
    auto defaults = json.find("defaults");
    if (defaults == json.end() || !defaults->is_object()) return {};
    if (!defaults->contains("wall_item_centre")) (*defaults)["wall_item_centre"] = kDefaultWallItemCentre;
    return {};
}

// Version 3 to 4: every room gets an empty wall material.
Status migratePlan3To4(Json& json)
{
    auto floors = json.find("floors");
    if (floors == json.end() || !floors->is_array()) return {};
    for (Json& floor : *floors) {
        if (!floor.is_object()) continue;
        auto rooms = floor.find("rooms");
        if (rooms == floor.end() || !rooms->is_array()) continue;
        for (Json& room : *rooms) {
            if (room.is_object() && !room.contains("wall_material")) room["wall_material"] = nullptr;
        }
    }
    return {};
}

// Version 3 to 4: the project gets the built-in default materials.
Status migrateProject3To4(Json& json)
{
    if (!json.is_object()) return {};
    if (!json.contains("materials")) json["materials"] = materialsToJson(MaterialDefaults{});
    return {};
}


// Version 4 to 5: floors get a slab, an image offset and no stairs, and the plan gets the default roof.
Status migratePlan4To5(Json& json)
{
    if (!json.is_object()) return {};
    if (auto floors = json.find("floors"); floors != json.end() && floors->is_array()) {
        for (Json& floor : *floors) {
            if (!floor.is_object()) continue;
            if (!floor.contains("slab_thickness")) floor["slab_thickness"] = kDefaultSlabThickness;
            if (!floor.contains("image_offset")) floor["image_offset"] = {{"x", 0.0}, {"y", 0.0}};
            if (!floor.contains("stairs")) floor["stairs"] = Json::array();
        }
    }
    if (!json.contains("roof")) json["roof"] = roofSettingsToJson(RoofSettings{});
    if (!json.contains("roof_sections")) json["roof_sections"] = Json::array();
    return {};
}

// Version 4 to 5: the materials get the roof, flat roof and stair defaults.
Status migrateProject4To5(Json& json)
{
    if (!json.is_object()) return {};
    auto materials = json.find("materials");
    if (materials == json.end() || !materials->is_object()) return {};
    const MaterialDefaults defaults;
    for (const Surface surface : {Surface::Roof, Surface::FlatRoof, Surface::Stair}) {
        const std::string key(surfaceName(surface));
        if (materials->contains(key)) continue;
        const auto& value = materialDefault(defaults, surface);
        (*materials)[key] = value ? Json(*value) : Json(nullptr);
    }
    return {};
}

// Version 5 to 6: every item gets an empty generator.
Status migratePlan5To6(Json& json)
{
    auto floors = json.find("floors");
    if (floors == json.end() || !floors->is_array()) return {};
    for (Json& floor : *floors) {
        if (!floor.is_object()) continue;
        auto items = floor.find("items");
        if (items == floor.end() || !items->is_array()) continue;
        for (Json& item : *items) {
            if (item.is_object() && !item.contains("generator")) item["generator"] = nullptr;
        }
    }
    return {};
}

// Version 6 to 7: floors get no lights, rooms get set dressing and openings pick catalog doors and windows.
Status migratePlan6To7(Json& json)
{
    auto floors = json.find("floors");
    if (floors == json.end() || !floors->is_array()) return {};
    for (Json& floor : *floors) {
        if (!floor.is_object()) continue;
        if (!floor.contains("lights")) floor["lights"] = Json::array();
        if (auto rooms = floor.find("rooms"); rooms != floor.end() && rooms->is_array()) {
            for (Json& room : *rooms) {
                if (room.is_object() && !room.contains("dressing")) room["dressing"] = true;
            }
        }
        if (auto openings = floor.find("openings"); openings != floor.end() && openings->is_array()) {
            for (Json& opening : *openings) {
                if (opening.is_object() && !opening.contains("auto_frame")) opening["auto_frame"] = true;
            }
        }
    }
    return {};
}

// Version 5 to 6: the materials get the trim and railing defaults, and the project gets trim and variation.
Status migrateProject5To6(Json& json)
{
    if (!json.is_object()) return {};
    if (auto materials = json.find("materials"); materials != json.end() && materials->is_object()) {
        const MaterialDefaults defaults;
        for (const Surface surface : {Surface::Trim, Surface::Railing}) {
            const std::string key(surfaceName(surface));
            if (materials->contains(key)) continue;
            const auto& value = materialDefault(defaults, surface);
            (*materials)[key] = value ? Json(*value) : Json(nullptr);
        }
    }
    if (!json.contains("trim")) json["trim"] = trimToJson(TrimSettings{});
    if (!json.contains("variation")) json["variation"] = variationToJson(VariationSettings{});
    return {};
}

// Adds a key to an object unless it is already there.
void addMissing(Json& object, const char* key, Json value)
{
    if (object.is_object() && !object.contains(key)) object[key] = std::move(value);
}

// Adds the plan format 8 fields to one set of roof settings.
void addRoofFields8(Json& settings)
{
    const RoofSettings defaults;
    addMissing(settings, "second_pitch", defaults.secondPitch);
    addMissing(settings, "break_ratio", defaults.breakRatio);
    addMissing(settings, "ridge_offset", defaults.ridgeOffset);
    addMissing(settings, "rise", defaults.rise);
}

// Adds an empty list to every object of a list that lacks it.
void addEmptyListToEach(Json& owner, const char* list, const char* key)
{
    auto it = owner.find(list);
    if (it == owner.end() || !it->is_array()) return;
    for (Json& entry : *it) addMissing(entry, key, Json::array());
}

// Version 7 to 8: walls get cladding, openings a fill, items what they stand on, stairs their outside step fields,
// roof settings the two pitch and curved fields, floors the new lists and the plan the ground floor, site and
// foundation.
Status migratePlan7To8(Json& json)
{
    if (!json.is_object()) return {};
    if (auto floors = json.find("floors"); floors != json.end() && floors->is_array()) {
        for (Json& floor : *floors) {
            if (!floor.is_object()) continue;
            addEmptyListToEach(floor, "walls", "cladding");
            if (auto openings = floor.find("openings"); openings != floor.end() && openings->is_array()) {
                for (Json& opening : *openings) addMissing(opening, "fill", nullptr);
            }
            if (auto items = floor.find("items"); items != floor.end() && items->is_array()) {
                for (Json& item : *items) addMissing(item, "on", nullptr);
            }
            if (auto stairs = floor.find("stairs"); stairs != floor.end() && stairs->is_array()) {
                for (Json& stair : *stairs) {
                    addMissing(stair, "rise", nullptr);
                    addMissing(stair, "cheeks", false);
                    addMissing(stair, "railing", true);
                }
            }
            for (const char* list : {"decks", "railings", "canopies", "dormers", "light_strips"}) addMissing(floor, list, Json::array());
        }
    }
    if (auto roof = json.find("roof"); roof != json.end() && roof->is_object()) addRoofFields8(*roof);
    if (auto sections = json.find("roof_sections"); sections != json.end() && sections->is_array()) {
        for (Json& section : *sections) {
            if (!section.is_object()) continue;
            if (auto settings = section.find("settings"); settings != section.end() && settings->is_object()) addRoofFields8(*settings);
        }
    }
    addMissing(json, "ground_floor", nullptr);
    addMissing(json, "site", siteToJson(Site{}));
    addMissing(json, "foundation", foundationToJson(Foundation{}));
    return {};
}

// Version 6 to 7: the materials get the deck, foundation, ground, paving, fence and boundary wall defaults.
Status migrateProject6To7(Json& json)
{
    if (!json.is_object()) return {};
    auto materials = json.find("materials");
    if (materials == json.end() || !materials->is_object()) return {};
    const MaterialDefaults defaults;
    for (const Surface surface : {Surface::Deck, Surface::Foundation, Surface::Ground, Surface::Paving, Surface::Fence,
                                  Surface::BoundaryWall}) {
        const std::string key(surfaceName(surface));
        if (materials->contains(key)) continue;
        const auto& value = materialDefault(defaults, surface);
        (*materials)[key] = value ? Json(*value) : Json(nullptr);
    }
    return {};
}

}

// The upgrade steps for plan.json.
std::span<const Migration> planMigrations()
{
    static const std::vector<Migration> steps = {{1, migratePlan1To2}, {2, migratePlan2To3}, {3, migratePlan3To4}, {4, migratePlan4To5}, {5, migratePlan5To6},
                                                  {6, migratePlan6To7}, {7, migratePlan7To8}};
    return steps;
}

// The upgrade steps for project.json.
std::span<const Migration> projectMigrations()
{
    static const std::vector<Migration> steps = {{1, migrateProject1To2}, {2, migrateProject2To3}, {3, migrateProject3To4}, {4, migrateProject4To5},
                                                  {5, migrateProject5To6}, {6, migrateProject6To7}};
    return steps;
}

}
