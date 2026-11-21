#include "plynth/core/OpeningShapes.h"

#include <algorithm>
#include <mutex>

namespace plynth {

namespace {

// The registry: every shape ever registered. Built as a function local static so that registering from
// namespace scope objects works in any start up order.
struct Registry {
    std::mutex mutex;
    std::vector<std::unique_ptr<OpeningShape>> shapes;
};

// The one registry.
Registry& registry()
{
    static Registry instance;
    return instance;
}

// Says whether text is a well formed shape id: lower case letters, digits and "_", starting with a letter.
bool wellFormedShapeId(std::string_view id)
{
    if (id.empty() || id.size() > 64) return false;
    if (id.front() < 'a' || id.front() > 'z') return false;
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

}

// Adds a shape unless its id is taken or malformed.
bool registerOpeningShape(std::unique_ptr<OpeningShape> shape)
{
    if (!shape || !wellFormedShapeId(shape->id())) return false;
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    for (const auto& existing : r.shapes) {
        if (existing->id() == shape->id()) return false;
    }
    r.shapes.push_back(std::move(shape));
    return true;
}

OpeningShapeRegistrar::OpeningShapeRegistrar(std::unique_ptr<OpeningShape> shape)
{
    registerOpeningShape(std::move(shape));
}

// Finds a shape by id.
const OpeningShape* findOpeningShape(std::string_view id)
{
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    for (const auto& shape : r.shapes) {
        if (shape->id() == id) return shape.get();
    }
    return nullptr;
}

// Every shape, sorted by sort key and then id.
std::vector<const OpeningShape*> allOpeningShapes()
{
    Registry& r = registry();
    std::vector<const OpeningShape*> out;
    {
        std::lock_guard lock(r.mutex);
        for (const auto& shape : r.shapes) out.push_back(shape.get());
    }
    std::sort(out.begin(), out.end(), [](const OpeningShape* a, const OpeningShape* b) {
        if (a->sortKey() != b->sortKey()) return a->sortKey() < b->sortKey();
        return a->id() < b->id();
    });
    return out;
}

// Checks that a shape exists and can be made at a size.
Status checkOpeningShape(std::string_view shapeId, double width, double height)
{
    if (!(std::isfinite(width) && width > 0.0 && std::isfinite(height) && height > 0.0)) {
        return fail(ErrorCode::InvalidValue, "The opening width and height must be numbers above zero.");
    }
    const OpeningShape* shape = findOpeningShape(shapeId);
    if (!shape) {
        std::string known;
        for (const OpeningShape* s : allOpeningShapes()) {
            if (!known.empty()) known += ", ";
            known += std::string(s->id());
        }
        return fail(ErrorCode::InvalidValue,
                    "The opening shape \"" + std::string(shapeId) + "\" is not known. Known shapes: " + known + ".");
    }
    if (!shape->fits(width, height)) {
        return fail(ErrorCode::InvalidValue, std::string(shape->sizeRule()));
    }
    return {};
}

// The outline of an opening in wall face coordinates.
std::vector<Vec2> openingOutline(const Opening& opening)
{
    if (!checkOpeningShape(opening.shape, opening.width, opening.height).ok()) return {};
    const OpeningShape* shape = findOpeningShape(opening.shape);
    std::vector<Vec2> out = shape->outline(opening.width, opening.height);
    for (Vec2& p : out) {
        p.x += opening.offset;
        p.y += opening.sill;
    }
    return out;
}

}
