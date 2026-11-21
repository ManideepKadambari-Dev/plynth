#pragma once

#include "Geometry.h"
#include "Plan.h"
#include "Status.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace plynth {

// Opening shapes are registry entries. A built-in shape is one file in core/src/openings/shapes/ with an
// OpeningShapeRegistrar at namespace scope, so adding one never means editing existing code. A custom shape
// is a shape file in Library/shapes/ (see readShapeFile), registered at run time with registerShapeFolder.
//
// Outlines are in the opening's own box: u runs along the wall from -width / 2 to width / 2 (left to right
// when the wall is seen from its right side, see WallSide), v runs up from 0 at the bottom of the opening to
// height. An outline is a simple closed polygon (the first point is not repeated), anticlockwise with v
// pointing up, inside the box and touching all four sides of it. Curved parts are cut with arcSegmentCount.

inline constexpr std::string_view kRectangleShapeId = "rectangle";
inline constexpr std::string_view kRoundArchShapeId = "round_arch";
inline constexpr std::string_view kFlatArchShapeId = "flat_arch";
inline constexpr std::string_view kPointedArchShapeId = "pointed_arch";
inline constexpr std::string_view kCircleShapeId = "circle";

// The sort key every custom shape gets, after the built-in ones.
inline constexpr int kCustomShapeSortKey = 1000;

// The built-in shapes, their sort keys and the sizes they need:
// - rectangle (10): the whole box. Any size.
// - round_arch (20): straight sides up to height - width / 2, then a half circle. Height at least width / 2.
// - flat_arch (30): straight sides up to height - width / 6, then a shallow arc through the top middle that
//   rises width / 6 above its ends. Height at least width / 6.
// - pointed_arch (40): straight sides up to height - width * sqrt(3) / 2, then two arcs of radius width,
//   each centred on the opposite side's top end, meeting at the top middle. Height at least
//   width * sqrt(3) / 2.
// - circle (50): the ellipse touching the four sides of the box, a circle when width equals height. Any size.
class OpeningShape {
public:
    virtual ~OpeningShape() = default;

    // Stable id stored in plan.json, for example "round_arch". Lower case letters, digits and "_".
    virtual std::string_view id() const = 0;
    // Short name for the app, for example "Round arch".
    virtual std::string_view name() const = 0;
    // One plain sentence on the size it needs, for example "The height must be at least half the width.".
    // Empty when any size works.
    virtual std::string_view sizeRule() const = 0;
    // Shapes are listed by this key, then by id. Built-in shapes use 10, 20, 30 and so on.
    virtual int sortKey() const = 0;
    // True for shapes compiled into Plynth, false for shapes read from a shape file.
    virtual bool builtIn() const = 0;
    // Says whether the shape can be made at this size. Width and height are finite and above zero.
    virtual bool fits(double width, double height) const = 0;
    // The outline at this size, as described above. Only called when fits is true.
    virtual std::vector<Vec2> outline(double width, double height) const = 0;
};

// Adds a shape to the registry. Returns false and drops it when its id is already taken or is not a well
// formed lower case name (letters a to z, digits and "_", starting with a letter).
bool registerOpeningShape(std::unique_ptr<OpeningShape> shape);

// Registers a shape while the program starts. A shape file holds one at namespace scope, for example
// const OpeningShapeRegistrar registrar(std::make_unique<RoundArchShape>());
struct OpeningShapeRegistrar {
    explicit OpeningShapeRegistrar(std::unique_ptr<OpeningShape> shape);
};

// The shape with this id. Null when there is none.
const OpeningShape* findOpeningShape(std::string_view id);

// Every registered shape, sorted by sortKey and then id.
std::vector<const OpeningShape*> allOpeningShapes();

// Checks that a shape exists and can be made at a size. InvalidValue when width or height is not finite
// and above zero, the shape is unknown (the message lists the known ids), or the size breaks the shape's
// rule (the message is its sizeRule).
Status checkOpeningShape(std::string_view shapeId, double width, double height);

// The outline of an opening in wall face coordinates: u along the wall from its from point (offset plus the
// shape's u), v up from the floor (sill plus the shape's v). Empty when the shape is unknown or the size
// does not fit it.
std::vector<Vec2> openingOutline(const Opening& opening);

// A custom shape file, Library/shapes/<id>.shape.json, in the format of schema/shape.schema.json:
// {"format": "plynth-shape", "version": 1, "id": "trefoil", "name": "Trefoil", "outline": [[0, 0], [1, 0], ...]}
// The outline is in a unit box: x from 0 (left) to 1 (right), y from 0 (bottom) to 1 (top). It is stretched to
// the opening's width and height, so local u = (x - 0.5) * width and v = y * height.
inline constexpr std::string_view kShapeFormatName = "plynth-shape";
inline constexpr int kShapeFormatVersion = 1;
inline constexpr std::string_view kShapeFileSuffix = ".shape.json";

// What reading a shape file gave.
struct ShapeFile {
    Status status;
    std::string id;
    std::string name;
    std::vector<Vec2> outline; // in the unit box, made anticlockwise with y up
};

// Reads and checks a shape file. CannotRead when it cannot be opened. BadFile when it is not valid JSON, the
// format or version is wrong, a field is missing or unknown, the id is not a well formed lower case name, the
// name is empty, or the outline has fewer than 3 or more than 1000 points, a number outside 0 to 1, a bounding
// box that is not exactly the unit box (within 1e-6), crossing edges, or no area.
ShapeFile readShapeFile(const std::filesystem::path& file);

// Makes a registry entry from a checked unit box outline. It has sortKey kCustomShapeSortKey, builtIn false,
// an empty sizeRule, fits every size, and stretches the outline to the box.
std::unique_ptr<OpeningShape> makeOutlineShape(std::string id, std::string name, std::vector<Vec2> unitOutline);

// What registering a folder of shape files did.
struct ShapeFolderLoad {
    std::vector<std::string> registered; // ids, in file name order
    std::vector<std::pair<std::filesystem::path, Status>> refused; // files that were bad or used a taken id
};

// Reads every file ending in kShapeFileSuffix in a folder (not its subfolders), in file name order, and
// registers each good one with makeOutlineShape. A file whose id is already registered is refused with
// AlreadyExists. A missing folder registers nothing and refuses nothing. Called once the library location is
// known (milestone 6); nothing calls it in milestone 5 except tests.
ShapeFolderLoad registerShapeFolder(const std::filesystem::path& folder);

}
