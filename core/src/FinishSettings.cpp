#include "plynth/core/FinishSettings.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>

namespace plynth {

namespace {

constexpr TrimKind kAllTrimKinds[] = {TrimKind::Skirting, TrimKind::Cornice, TrimKind::Casing, TrimKind::Sill,
                                      TrimKind::Railing};

// Says whether text is a well formed profile id: lower case letters, digits and "_", starting with a letter.
bool wellFormedProfileId(const std::string& id)
{
    if (id.empty() || id.size() > 64 || id.front() < 'a' || id.front() > 'z') return false;
    return std::all_of(id.begin(), id.end(),
                       [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

// Says whether a size is a number within the trim range.
bool trimSizeInRange(double value)
{
    return std::isfinite(value) && value >= kMinTrimSize - 1e-12 && value <= kMaxTrimSize + 1e-12;
}

}

// The style of one kind of trim.
TrimStyle& trimStyle(TrimSettings& settings, TrimKind kind)
{
    switch (kind) {
    case TrimKind::Skirting: return settings.skirting;
    case TrimKind::Cornice: return settings.cornice;
    case TrimKind::Casing: return settings.casing;
    case TrimKind::Sill: return settings.sill;
    case TrimKind::Railing: return settings.railing;
    }
    return settings.skirting;
}

const TrimStyle& trimStyle(const TrimSettings& settings, TrimKind kind)
{
    return trimStyle(const_cast<TrimSettings&>(settings), kind);
}

// Checks every trim style and the railing height.
Status checkTrimSettings(const TrimSettings& settings)
{
    for (TrimKind kind : kAllTrimKinds) {
        const TrimStyle& style = trimStyle(settings, kind);
        const std::string name(trimKindName(kind));
        if (!wellFormedProfileId(style.profile)) {
            return fail(ErrorCode::InvalidValue,
                        "The " + name + " profile must use lower case letters, digits and \"_\" and start with a letter.");
        }
        if (!trimSizeInRange(style.height) || !trimSizeInRange(style.depth)) {
            return fail(ErrorCode::InvalidValue, "The " + name + " height and depth must be from 0.005 to 0.5 m.");
        }
    }
    if (!std::isfinite(settings.railingHeight) || settings.railingHeight < kMinRailingHeight - 1e-12
        || settings.railingHeight > kMaxRailingHeight + 1e-12) {
        return fail(ErrorCode::InvalidValue, "The railing height must be from 0.5 to 1.5 m.");
    }
    return {};
}

// The file name of a trim kind.
std::string_view trimKindName(TrimKind kind)
{
    switch (kind) {
    case TrimKind::Skirting: return "skirting";
    case TrimKind::Cornice: return "cornice";
    case TrimKind::Casing: return "casing";
    case TrimKind::Sill: return "sill";
    case TrimKind::Railing: return "railing";
    }
    return "skirting";
}

// The trim kind for a file name.
std::optional<TrimKind> trimKindFromName(std::string_view name)
{
    for (TrimKind kind : kAllTrimKinds) {
        if (trimKindName(kind) == name) return kind;
    }
    return std::nullopt;
}

// The file name of a light kind.
std::string_view lightKindName(LightKind kind)
{
    switch (kind) {
    case LightKind::Ceiling: return "ceiling";
    case LightKind::Wall: return "wall";
    case LightKind::Standing: return "standing";
    }
    return "ceiling";
}

// The light kind for a file name.
std::optional<LightKind> lightKindFromName(std::string_view name)
{
    for (LightKind kind : {LightKind::Ceiling, LightKind::Wall, LightKind::Standing}) {
        if (lightKindName(kind) == name) return kind;
    }
    return std::nullopt;
}

// The file name of a light strip kind.
std::string_view lightStripKindName(LightStripKind kind)
{
    switch (kind) {
    case LightStripKind::Cove: return "cove";
    case LightStripKind::Soffit: return "soffit";
    case LightStripKind::Edge: return "edge";
    case LightStripKind::Step: return "step";
    }
    return "cove";
}

// The light strip kind for a file name.
std::optional<LightStripKind> lightStripKindFromName(std::string_view name)
{
    for (LightStripKind kind : {LightStripKind::Cove, LightStripKind::Soffit, LightStripKind::Edge, LightStripKind::Step}) {
        if (lightStripKindName(kind) == name) return kind;
    }
    return std::nullopt;
}

// The height a new light marker starts at.
double defaultLightHeight(LightKind kind, double ceilingHeight)
{
    const double top = ceilingHeight - 0.1;
    double height = top;
    if (kind == LightKind::Wall) height = 1.8;
    if (kind == LightKind::Standing) height = 1.4;
    return std::max(0.0, std::min(height, top));
}

}
