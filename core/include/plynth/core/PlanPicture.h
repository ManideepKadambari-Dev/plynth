#pragma once

#include "BuildProblem.h"
#include "Geometry.h"
#include "Plan.h"
#include "Project.h"
#include "Status.h"
#include "detection/GrayImage.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// A picture of one floor's plan with its marks drawn on top, the way the app's plan view shows them, so a
// script or an assistant can check detection and marks against the drawing without the app ("plynth render
// --plan"). It needs no build and no graphics driver, and it changes nothing in the project.
//
// What is drawn, from the bottom up: the floor's plan image dimmed (only when the plan has a scale, since the
// marks are in metres), an optional 1 m grid, rooms filled light blue, walls as blue bands of their thickness
// with the openings cut out, door leaves and swings, window glass lines, stairs, item footprints with a tick on
// their front edge, light markers, points as dots, then labels with the plan ids ("w3", "r2", "o5", "i4"), and
// an optional scale bar.

inline constexpr int kDefaultPlanPictureSide = 2000;
inline constexpr int kMinPlanPictureSide = 64;
inline constexpr int kMaxPlanPictureSide = 4096;

struct PlanPictureOptions {
    int longSide = kDefaultPlanPictureSide; // pixels on the longer side, kMinPlanPictureSide to kMaxPlanPictureSide
    bool image = true;  // draw the floor's plan image under the marks
    bool labels = true; // draw the id labels and the title
    bool grid = false;  // draw a 1 m grid and a scale bar
};

// A picture held in memory: 3 bytes per pixel (red, green, blue), row after row from the top left corner.
struct RgbImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;

    // The red, green and blue of one pixel. The position must be inside the picture.
    std::array<std::uint8_t, 3> at(int x, int y) const;
};

// One label in the picture. kind is "floor", "point", "wall", "opening", "room", "item", "stair" or "light".
// The box is in picture pixels, x and y its top left corner. text holds one line per label line, joined by "\n".
struct PlanLabel {
    Id id;
    std::string kind;
    std::string text;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct PlanPicture {
    Status status;                  // InvalidValue for a size out of range
    RgbImage image;
    double pixelsPerMetre = 0.0;    // picture pixels per plan metre
    Vec2 origin;                    // the plan position of the picture's top left corner, metres
    bool imageShown = false;        // the plan image is under the marks
    std::vector<PlanLabel> labels;  // in the order they were placed
};

// Where a plan position lands in the picture, in pixels.
Vec2 planToPicture(const PlanPicture& picture, Vec2 position);

// Draws one floor of the plan. background is the floor's plan image, or null for none; it is drawn only when
// options.image is set and the plan has a scale. The picture covers the marks and the image with a margin, and
// a 10 m square from (0, 0) when there is neither.
PlanPicture drawPlanPicture(const Plan& plan, const Floor& floor, const GrayImage* background,
                            const PlanPictureOptions& options);

// What "plynth render --plan" asks for.
struct PlanRenderOptions {
    PlanPictureOptions picture;
    Id floor; // empty: the first floor
    // Empty: renders/plan-<floor>.png in the project folder. Relative: inside the project folder. Absolute: as
    // it is. It must end in .png and may not be inside the project's plans/, build/, export/ or pack/ folders.
    std::filesystem::path out;
};

struct PlanRenderReport {
    Status status; // NotAProject, NotFound for an unknown floor, InvalidValue, CannotWrite
    std::filesystem::path file; // the PNG written, absolute; empty on failure
    int width = 0;
    int height = 0;
    double pixelsPerMetre = 0.0;
    Vec2 origin;
    Id floor;
    bool imageShown = false;
    std::vector<PlanLabel> labels;
    // Warnings: no_scale (the image is not drawn because the plan has no scale), image_missing (the floor's
    // image could not be read).
    std::vector<BuildProblem> problems;
};

// Loads the floor's plan image from the project folder when it is wanted, draws the picture and saves it as a
// PNG through a temporary file, so a failure leaves an old picture as it was. Reads only the plan it is given.
PlanRenderReport renderPlanPicture(const std::filesystem::path& folder, const Project& project,
                                   const PlanRenderOptions& options);

// The result of "plynth render --plan": file, width, height, scale (pixels per metre), origin {x, y} in metres
// (the plan position of the top left pixel), floor, image (whether the plan image is drawn), labels. Paths are
// absolute, UTF-8, forward slashes.
Json planRenderReportToJson(const PlanRenderReport& report);

}
