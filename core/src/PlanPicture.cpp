#include "plynth/core/PlanPicture.h"

#include "FileIo.h"

#include "plynth/core/Items.h"
#include "plynth/core/Openings.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/Render.h"
#include "plynth/core/Rooms.h"
#include "plynth/core/Stairs.h"
#include "plynth/core/WallGeometry.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <exception>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// Theme colours and opacities. Core cannot read the app's theme file, which is built into the app, so the few
// it needs are copied here from app/resources/themes/drafting-dark.json. Keep them the same as that file.
struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};
constexpr Rgb kGround{0x0E, 0x11, 0x16};
constexpr Rgb kPanel{0x15, 0x1A, 0x21};
constexpr Rgb kRaised{0x1B, 0x22, 0x2C};
constexpr Rgb kLine{0x27, 0x30, 0x3C};
constexpr Rgb kText{0xE6, 0xEA, 0xF0};
constexpr Rgb kMuted{0x9A, 0xA6, 0xB5};
constexpr Rgb kSignalBlue{0x4C, 0xC2, 0xFF};
constexpr int kOverlayAlpha = 150;  // effects.overlayAlpha: wall bands and the grid
constexpr int kReadOnlyDim = 150;   // effects.readOnlyDim: how far the plan image is dimmed toward Ground
constexpr int kRoomFillAlpha = 36;  // effects.roomFillAlpha
constexpr int kLabelBacking = 230;  // the label backing alpha the plan view uses

constexpr int kShift = 4; // OpenCV sub pixel bits for smooth shapes
constexpr double kSubPixel = 16.0;
constexpr int kArcSteps = 16;

const std::array<std::wstring_view, 4> kGuardedFolders = {L"plans", L"build", L"export", L"pack"};

// An OpenCV colour in the picture's red, green, blue order.
cv::Scalar scalar(Rgb c)
{
    return cv::Scalar(c.r, c.g, c.b);
}

// Everything the drawing steps share: the canvas and how plan metres map onto it.
struct Canvas {
    cv::Mat rgb;
    Vec2 origin;
    double scale = 1.0;
    int stroke = 1;
    double fontScale = 0.5;
    int fontThickness = 1;
    int dot = 3;

    // A plan position in picture pixels.
    Vec2 toPicture(Vec2 p) const { return {(p.x - origin.x) * scale, (p.y - origin.y) * scale}; }

    // A plan position as a sub pixel OpenCV point.
    cv::Point fine(Vec2 p) const
    {
        const Vec2 q = toPicture(p);
        return {static_cast<int>(std::lround(q.x * kSubPixel)), static_cast<int>(std::lround(q.y * kSubPixel))};
    }

    // A ring of plan positions as sub pixel OpenCV points.
    std::vector<cv::Point> fineRing(const std::vector<Vec2>& ring) const
    {
        std::vector<cv::Point> out;
        out.reserve(ring.size());
        for (const Vec2& p : ring) out.push_back(fine(p));
        return out;
    }
};

// Mixes a colour into the picture wherever the mask is set, by mask times alpha.
void blendMask(cv::Mat& rgb, const cv::Mat& mask, Rgb colour, int alpha)
{
    const double c[3] = {static_cast<double>(colour.r), static_cast<double>(colour.g), static_cast<double>(colour.b)};
    for (int y = 0; y < rgb.rows; ++y) {
        std::uint8_t* p = rgb.ptr<std::uint8_t>(y);
        const std::uint8_t* m = mask.ptr<std::uint8_t>(y);
        for (int x = 0; x < rgb.cols; ++x) {
            if (m[x] == 0) continue;
            const double f = m[x] * alpha / (255.0 * 255.0);
            for (int k = 0; k < 3; ++k) {
                p[3 * x + k] = static_cast<std::uint8_t>(std::lround(p[3 * x + k] * (1.0 - f) + c[k] * f));
            }
        }
    }
}

// Mixes a colour over a rectangle of the picture.
void blendRect(cv::Mat& rgb, cv::Rect box, Rgb colour, int alpha)
{
    box &= cv::Rect(0, 0, rgb.cols, rgb.rows);
    if (box.empty()) return;
    cv::Mat part = rgb(box);
    const cv::Mat mask(box.size(), CV_8UC1, cv::Scalar(255));
    blendMask(part, mask, colour, alpha);
}

// Text the Hershey font can draw: printable ASCII, anything else as "?".
std::string printable(std::string_view text)
{
    std::string out;
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (u >= 0x80) {
            if ((u & 0xC0) != 0x80) out += '?';
        } else {
            out += (u >= 0x20 && u < 0x7F) ? c : ' ';
        }
    }
    return out;
}

// A number with a fixed count of decimals.
std::string fixed(double value, int decimals)
{
    char text[48];
    std::snprintf(text, sizeof(text), "%.*f", decimals, value);
    return text;
}

// A number without trailing zeros, for messages.
std::string number(double value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%g", value);
    return text;
}

// The middle of a polyline, measured along it.
Vec2 middleOf(const std::vector<Vec2>& line)
{
    if (line.empty()) return {};
    double total = 0.0;
    for (std::size_t i = 1; i < line.size(); ++i) total += distance(line[i - 1], line[i]);
    double left = total / 2.0;
    for (std::size_t i = 1; i < line.size(); ++i) {
        const double piece = distance(line[i - 1], line[i]);
        if (piece >= left && piece > 0.0) {
            const double t = left / piece;
            return {line[i - 1].x + (line[i].x - line[i - 1].x) * t, line[i - 1].y + (line[i].y - line[i - 1].y) * t};
        }
        left -= piece;
    }
    return line.back();
}

// The smallest box around a set of plan positions, grown as positions are added.
struct Bounds {
    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();

    // Takes one more position in, ignoring ones that are not finite.
    void add(Vec2 p)
    {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return;
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }

    // Takes every position of a ring in.
    void addRing(const std::vector<Vec2>& ring)
    {
        for (const Vec2& p : ring) add(p);
    }

    bool empty() const { return !(minX <= maxX && minY <= maxY); }
};

// Places label boxes so they do not cover each other, and draws them.
class Labeller {
public:
    explicit Labeller(Canvas& canvas) : canvas_(canvas) {}

    // Puts a label near an anchor in picture pixels: centred on it when that spot is free, else beside it. A
    // label that may be dropped is left out when no free spot is found; any other one goes on the centre.
    void add(const Id& id, std::string kind, const std::vector<std::string>& lines, Vec2 anchor, bool quiet,
             bool droppable, std::vector<PlanLabel>& out)
    {
        const double fontScale = quiet ? canvas_.fontScale * 0.8 : canvas_.fontScale;
        const int thickness = quiet ? 1 : canvas_.fontThickness;
        int width = 0;
        int lineHeight = 0;
        for (const std::string& line : lines) {
            int baseline = 0;
            const cv::Size size = cv::getTextSize(line, cv::FONT_HERSHEY_SIMPLEX, fontScale, thickness, &baseline);
            width = std::max(width, size.width);
            lineHeight = std::max(lineHeight, size.height);
        }
        const int pad = std::max(2, lineHeight / 4);
        const int gap = std::max(2, lineHeight / 3);
        const int count = static_cast<int>(lines.size());
        const int boxWidth = width + 2 * pad;
        const int boxHeight = count * lineHeight + (count - 1) * gap + 2 * pad;

        const double reach = canvas_.dot + 3.0;
        const double sideX = boxWidth / 2.0 + reach;
        const double sideY = boxHeight / 2.0 + reach;
        const std::array<Vec2, 9> offsets = {Vec2{0.0, 0.0}, Vec2{0.0, -sideY},  Vec2{0.0, sideY},
                                             Vec2{sideX, 0.0}, Vec2{-sideX, 0.0}, Vec2{sideX, -sideY},
                                             Vec2{-sideX, -sideY}, Vec2{sideX, sideY}, Vec2{-sideX, sideY}};
        const cv::Rect picture(0, 0, canvas_.rgb.cols, canvas_.rgb.rows);
        std::optional<cv::Rect> chosen;
        for (const Vec2& offset : offsets) {
            const cv::Rect box(static_cast<int>(std::lround(anchor.x + offset.x - boxWidth / 2.0)),
                               static_cast<int>(std::lround(anchor.y + offset.y - boxHeight / 2.0)), boxWidth, boxHeight);
            if ((box & picture) != box) continue;
            const bool clear = std::none_of(taken_.begin(), taken_.end(), [&](const cv::Rect& t) { return (box & t).area() > 0; });
            if (clear) {
                chosen = box;
                break;
            }
        }
        if (!chosen) {
            if (droppable) return;
            cv::Rect box(static_cast<int>(std::lround(anchor.x - boxWidth / 2.0)),
                         static_cast<int>(std::lround(anchor.y - boxHeight / 2.0)), boxWidth, boxHeight);
            box.x = std::clamp(box.x, 0, std::max(0, picture.width - box.width));
            box.y = std::clamp(box.y, 0, std::max(0, picture.height - box.height));
            chosen = box;
        }
        taken_.push_back(*chosen);

        blendRect(canvas_.rgb, *chosen, kPanel, kLabelBacking);
        const Rgb colour = quiet ? kMuted : kText;
        for (int i = 0; i < count; ++i) {
            const cv::Point at(chosen->x + pad, chosen->y + pad + lineHeight + i * (lineHeight + gap));
            cv::putText(canvas_.rgb, lines[static_cast<std::size_t>(i)], at, cv::FONT_HERSHEY_SIMPLEX, fontScale,
                        scalar(colour), thickness, cv::LINE_AA);
        }

        PlanLabel label;
        label.id = id;
        label.kind = std::move(kind);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (i > 0) label.text += '\n';
            label.text += lines[i];
        }
        label.x = chosen->x;
        label.y = chosen->y;
        label.width = chosen->width;
        label.height = chosen->height;
        out.push_back(std::move(label));
    }

    // Keeps a spot free, for something drawn outside the labeller.
    void reserve(cv::Rect box) { taken_.push_back(box); }

private:
    Canvas& canvas_;
    std::vector<cv::Rect> taken_;
};

// The floor's image as a plan area, when it is to be drawn.
struct ImageArea {
    const GrayImage* image = nullptr;
    double pixelsPerMetre = 0.0;
};

// Draws the plan image dimmed toward Ground, over the Raised sheet.
void drawImage(Canvas& canvas, const Floor& floor, const ImageArea& area)
{
    const GrayImage& image = *area.image;
    const cv::Mat gray(image.height, image.width, CV_8UC1, const_cast<std::uint8_t*>(image.pixels.data()));
    const double k = canvas.scale / area.pixelsPerMetre;
    cv::Mat source = gray;
    double kx = k;
    double ky = k;
    if (k < 1.0) {
        const int w = static_cast<int>(std::lround(image.width * k));
        const int h = static_cast<int>(std::lround(image.height * k));
        if (w < 1 || h < 1) return;
        cv::resize(gray, source, cv::Size(w, h), 0.0, 0.0, cv::INTER_AREA);
        kx = k * image.width / source.cols;
        ky = k * image.height / source.rows;
    }
    const Vec2 corner = canvas.toPicture({floor.imageOffset.x, floor.imageOffset.y});
    const cv::Mat move = (cv::Mat_<double>(2, 3) << kx, 0.0, corner.x, 0.0, ky, corner.y);
    cv::Mat paper;
    cv::warpAffine(source, paper, move, canvas.rgb.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(255));
    const cv::Mat full(source.size(), CV_8UC1, cv::Scalar(255));
    cv::Mat cover;
    cv::warpAffine(full, cover, move, canvas.rgb.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));

    const double dim = kReadOnlyDim / 255.0;
    const double ground[3] = {static_cast<double>(kGround.r), static_cast<double>(kGround.g), static_cast<double>(kGround.b)};
    for (int y = 0; y < canvas.rgb.rows; ++y) {
        std::uint8_t* p = canvas.rgb.ptr<std::uint8_t>(y);
        const std::uint8_t* g = paper.ptr<std::uint8_t>(y);
        const std::uint8_t* c = cover.ptr<std::uint8_t>(y);
        for (int x = 0; x < canvas.rgb.cols; ++x) {
            if (c[x] == 0) continue;
            const double f = c[x] / 255.0;
            for (int i = 0; i < 3; ++i) {
                const double ink = g[x] * (1.0 - dim) + ground[i] * dim;
                p[3 * x + i] = static_cast<std::uint8_t>(std::lround(p[3 * x + i] * (1.0 - f) + ink * f));
            }
        }
    }
}

// The step between grid lines: 1 m, or 5 m, 25 m and so on when lines would come closer than 8 pixels.
double gridStep(double scale)
{
    double step = 1.0;
    while (step * scale < 8.0) step *= 5.0;
    return step;
}

// Draws the grid: thin lines at each step, stronger ones at every fifth.
void drawGrid(Canvas& canvas)
{
    const double step = gridStep(canvas.scale);
    const double width = canvas.rgb.cols / canvas.scale;
    const double height = canvas.rgb.rows / canvas.scale;
    cv::Mat minor = cv::Mat::zeros(canvas.rgb.size(), CV_8UC1);
    cv::Mat major = cv::Mat::zeros(canvas.rgb.size(), CV_8UC1);
    const auto isMajor = [&](double at) { return std::fmod(std::abs(std::round(at / step)), 5.0) == 0.0; };
    for (double x = std::ceil(canvas.origin.x / step) * step; x <= canvas.origin.x + width; x += step) {
        cv::line(isMajor(x) ? major : minor, canvas.fine({x, canvas.origin.y}), canvas.fine({x, canvas.origin.y + height}),
                 cv::Scalar(255), 1, cv::LINE_8, kShift);
    }
    for (double y = std::ceil(canvas.origin.y / step) * step; y <= canvas.origin.y + height; y += step) {
        cv::line(isMajor(y) ? major : minor, canvas.fine({canvas.origin.x, y}), canvas.fine({canvas.origin.x + width, y}),
                 cv::Scalar(255), 1, cv::LINE_8, kShift);
    }
    blendMask(canvas.rgb, minor, kLine, kOverlayAlpha);
    blendMask(canvas.rgb, major, kMuted, 90);
}

// Draws the scale bar in the bottom left corner: a round length near a fifth of the picture's width.
void drawScaleBar(Canvas& canvas, Labeller& labeller)
{
    const double wanted = canvas.rgb.cols / canvas.scale / 5.0;
    double length = 0.1;
    for (const double candidate : {0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0}) {
        if (candidate <= wanted) length = candidate;
    }
    const int barPixels = static_cast<int>(std::lround(length * canvas.scale));
    const std::string text = number(length) + " m";
    int baseline = 0;
    const cv::Size size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, canvas.fontScale, canvas.fontThickness, &baseline);
    const int pad = std::max(4, size.height / 2);
    const int tick = std::max(4, size.height / 2);
    const int boxWidth = std::max(barPixels, size.width) + 2 * pad;
    const int boxHeight = size.height + tick + 3 * pad;
    const cv::Rect box(pad, canvas.rgb.rows - boxHeight - pad, boxWidth, boxHeight);
    blendRect(canvas.rgb, box, kPanel, kLabelBacking);
    const int left = box.x + pad;
    const int barY = box.y + box.height - pad;
    cv::line(canvas.rgb, {left, barY}, {left + barPixels, barY}, scalar(kText), std::max(2, canvas.stroke), cv::LINE_AA);
    cv::line(canvas.rgb, {left, barY}, {left, barY - tick}, scalar(kText), canvas.stroke, cv::LINE_AA);
    cv::line(canvas.rgb, {left + barPixels, barY}, {left + barPixels, barY - tick}, scalar(kText), canvas.stroke, cv::LINE_AA);
    cv::putText(canvas.rgb, text, {left, box.y + pad + size.height}, cv::FONT_HERSHEY_SIMPLEX, canvas.fontScale,
                scalar(kText), canvas.fontThickness, cv::LINE_AA);
    labeller.reserve(box);
}

// The unit direction from a to b, or zero when they are the same place.
Vec2 unit(Vec2 a, Vec2 b)
{
    const double length = distance(a, b);
    if (length < 1e-12) return {};
    return {(b.x - a.x) / length, (b.y - a.y) / length};
}

// A polyline moved sideways by a distance, positive to its right.
std::vector<Vec2> shifted(const std::vector<Vec2>& line, double by)
{
    std::vector<Vec2> out;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const Vec2 before = line[i == 0 ? 0 : i - 1];
        const Vec2 after = line[i + 1 < line.size() ? i + 1 : i];
        const Vec2 n = rightOf(unit(before, after));
        out.push_back({line[i].x + n.x * by, line[i].y + n.y * by});
    }
    return out;
}

// Draws an open polyline of plan positions.
void polyline(Canvas& canvas, const std::vector<Vec2>& line, Rgb colour, int thickness, bool closed = false)
{
    if (line.size() < 2) return;
    const std::vector<std::vector<cv::Point>> lines = {canvas.fineRing(line)};
    cv::polylines(canvas.rgb, lines, closed, scalar(colour), thickness, cv::LINE_AA, kShift);
}

// Draws the door leaf and swing, the window glass lines, and the jambs of every opening that has a place.
void drawOpeningMarks(Canvas& canvas, const Floor& floor)
{
    for (const Opening& opening : floor.openings) {
        const OpeningPlacement place = openingPlacement(floor, opening);
        if (!place.valid) continue;
        const double half = place.thickness / 2.0;
        for (const auto& [at, tangent] : {std::pair{place.start, place.startTangent}, std::pair{place.end, place.endTangent}}) {
            const Vec2 n = rightOf(tangent);
            polyline(canvas, {{at.x + n.x * half, at.y + n.y * half}, {at.x - n.x * half, at.y - n.y * half}}, kSignalBlue,
                     canvas.stroke);
        }
        if (opening.type == OpeningType::Door) {
            const bool hingeAtStart = opening.hinge == HingeEnd::Start;
            const Vec2 hinge = hingeAtStart ? place.start : place.end;
            const Vec2 other = hingeAtStart ? place.end : place.start;
            const Vec2 along = unit(hinge, other);
            Vec2 side = rightOf(unit(place.start, place.end));
            if (opening.swing == WallSide::Left) side = {-side.x, -side.y};
            const double radius = opening.width;
            polyline(canvas, {hinge, {hinge.x + side.x * radius, hinge.y + side.y * radius}}, kSignalBlue, canvas.stroke);
            std::vector<Vec2> arc;
            for (int i = 0; i <= kArcSteps; ++i) {
                const double angle = (std::numbers::pi / 2.0) * i / kArcSteps;
                const double c = std::cos(angle) * radius;
                const double s = std::sin(angle) * radius;
                arc.push_back({hinge.x + along.x * c + side.x * s, hinge.y + along.y * c + side.y * s});
            }
            polyline(canvas, arc, kSignalBlue, std::max(1, canvas.stroke - 1));
        } else if (opening.type == OpeningType::Window) {
            polyline(canvas, shifted(place.centreLine, place.thickness / 6.0), kSignalBlue, std::max(1, canvas.stroke - 1));
            polyline(canvas, shifted(place.centreLine, -place.thickness / 6.0), kSignalBlue, std::max(1, canvas.stroke - 1));
        }
    }
}

// Draws the stairs that start on this floor and the outlines of those that arrive from the floor below.
void drawStairs(Canvas& canvas, const Plan& plan, const Floor& floor)
{
    for (const StairLayout& arriving : stairsArrivingAt(plan, floor.id)) {
        if (arriving.footprint.size() < 3) continue;
        cv::Mat mask = cv::Mat::zeros(canvas.rgb.size(), CV_8UC1);
        const std::vector<std::vector<cv::Point>> rings = {canvas.fineRing(arriving.footprint)};
        cv::fillPoly(mask, rings, cv::Scalar(255), cv::LINE_AA, kShift);
        blendMask(canvas.rgb, mask, kSignalBlue, kRoomFillAlpha / 2);
        polyline(canvas, arriving.footprint, kSignalBlue, std::max(1, canvas.stroke - 1), true);
    }
    for (const Stair& stair : floor.stairs) {
        const StairLayout layout = stairLayout(plan, floor, stair);
        if (!layout.status.ok()) {
            Item probe;
            probe.rotation = stair.rotation;
            const Vec2 front = itemFront(probe);
            const Vec2 start{stair.x, stair.y};
            cv::arrowedLine(canvas.rgb, canvas.fine(start), canvas.fine({start.x + front.x, start.y + front.y}),
                            scalar(kSignalBlue), canvas.stroke, cv::LINE_AA, kShift, 0.2);
            continue;
        }
        for (const StairStep& step : layout.steps) {
            polyline(canvas, step.outline, kMuted, 1, true);
        }
        polyline(canvas, layout.footprint, kSignalBlue, canvas.stroke, true);
        if (layout.walkLine.size() >= 2) {
            polyline(canvas, layout.walkLine, kSignalBlue, canvas.stroke);
            const Vec2 a = layout.walkLine[layout.walkLine.size() - 2];
            const Vec2 b = layout.walkLine.back();
            const double tip = 0.3 / std::max(distance(a, b), 1e-9);
            cv::arrowedLine(canvas.rgb, canvas.fine(a), canvas.fine(b), scalar(kSignalBlue), canvas.stroke, cv::LINE_AA,
                            kShift, std::min(1.0, tip));
        }
    }
}

// Draws every item as its footprint outline in Muted, with a tick on its front edge.
void drawItems(Canvas& canvas, const Floor& floor)
{
    for (const Item& item : floor.items) {
        const ItemFootprint footprint = itemFootprint(item);
        const std::vector<Vec2> corners(footprint.corners.begin(), footprint.corners.end());
        polyline(canvas, corners, kMuted, canvas.stroke, true);
        const Vec2 frontMid{(corners[2].x + corners[3].x) / 2.0, (corners[2].y + corners[3].y) / 2.0};
        const double tick = std::min(item.size.depth, item.size.width) * 0.25;
        polyline(canvas, {frontMid, {frontMid.x - footprint.front.x * tick, frontMid.y - footprint.front.y * tick}}, kMuted,
                 canvas.stroke);
    }
}

// Draws every light marker as a small ring.
void drawLights(Canvas& canvas, const Floor& floor)
{
    for (const LightMarker& light : floor.lights) {
        const int radius = static_cast<int>(std::lround((canvas.dot + 2) * kSubPixel));
        cv::circle(canvas.rgb, canvas.fine({light.x, light.y}), radius, scalar(kText), canvas.stroke, cv::LINE_AA, kShift);
    }
}

// Draws every point as a blue dot with a dark rim, so it shows on a wall band too.
void drawPoints(Canvas& canvas, const Floor& floor)
{
    const int radius = static_cast<int>(std::lround(canvas.dot * kSubPixel));
    for (const Point& point : floor.points) {
        const cv::Point at = canvas.fine(positionOf(point));
        cv::circle(canvas.rgb, at, radius + static_cast<int>(kSubPixel), scalar(kGround), cv::FILLED, cv::LINE_AA, kShift);
        cv::circle(canvas.rgb, at, radius, scalar(kSignalBlue), cv::FILLED, cv::LINE_AA, kShift);
    }
}

// Adds the labels, from the most to the least important, so point labels give way to the others.
void drawLabels(Canvas& canvas, Labeller& labeller, const Floor& floor, std::vector<PlanLabel>& out)
{
    const auto at = [&](Vec2 p) { return canvas.toPicture(p); };
    for (const Room& room : floor.rooms) {
        const RoomShape shape = roomShape(floor, room);
        if (!shape.closed) continue;
        const std::string type = room.type ? printable(*room.type) : std::string("no type");
        labeller.add(room.id, "room", {printable(room.id) + " " + type, fixed(shape.area, 2) + " m2"}, at(shape.labelPoint),
                     false, false, out);
    }
    for (const Wall& wall : floor.walls) {
        const WallPath path = wallPath(floor, wall);
        if (path.centreLine.size() < 2) continue;
        labeller.add(wall.id, "wall", {printable(wall.id)}, at(middleOf(path.centreLine)), false, false, out);
    }
    for (const Opening& opening : floor.openings) {
        const OpeningPlacement place = openingPlacement(floor, opening);
        if (!place.valid) continue;
        labeller.add(opening.id, "opening", {printable(opening.id) + " " + std::string(openingTypeName(opening.type))},
                     at(place.centre), false, false, out);
    }
    for (const Stair& stair : floor.stairs) {
        labeller.add(stair.id, "stair", {printable(stair.id) + " " + printable(stair.type)}, at({stair.x, stair.y}), false,
                     false, out);
    }
    for (const Item& item : floor.items) {
        const std::string what = item.generator ? item.generator->name : item.asset;
        labeller.add(item.id, "item", {printable(item.id) + " " + printable(what)}, at({item.x, item.y}), false, false, out);
    }
    for (const LightMarker& light : floor.lights) {
        labeller.add(light.id, "light", {printable(light.id)}, at({light.x, light.y}), true, true, out);
    }
    for (const Point& point : floor.points) {
        labeller.add(point.id, "point", {printable(point.id)}, at(positionOf(point)), true, true, out);
    }
}

// The plan area the picture shows: the marks and the image, with a margin.
Bounds pictureArea(const Plan& plan, const Floor& floor, const ImageArea& image)
{
    Bounds bounds;
    for (const Point& point : floor.points) bounds.add(positionOf(point));
    for (const Wall& wall : floor.walls) {
        for (const auto& ring : wallOutline(floor, wall)) bounds.addRing(ring);
    }
    for (const Opening& opening : floor.openings) {
        const OpeningPlacement place = openingPlacement(floor, opening);
        if (!place.valid || opening.type != OpeningType::Door) continue;
        const double r = opening.width;
        for (const Vec2 jamb : {place.start, place.end}) {
            bounds.add({jamb.x - r, jamb.y - r});
            bounds.add({jamb.x + r, jamb.y + r});
        }
    }
    for (const Item& item : floor.items) {
        const ItemFootprint footprint = itemFootprint(item);
        for (const Vec2& corner : footprint.corners) bounds.add(corner);
    }
    for (const Stair& stair : floor.stairs) {
        const StairLayout layout = stairLayout(plan, floor, stair);
        bounds.add({stair.x, stair.y});
        if (layout.status.ok()) bounds.addRing(layout.footprint);
    }
    for (const StairLayout& arriving : stairsArrivingAt(plan, floor.id)) bounds.addRing(arriving.footprint);
    for (const LightMarker& light : floor.lights) bounds.add({light.x, light.y});
    if (image.image != nullptr) {
        bounds.add(imageToPlan(floor, image.pixelsPerMetre, {0.0, 0.0}));
        bounds.add(imageToPlan(floor, image.pixelsPerMetre,
                               {static_cast<double>(image.image->width), static_cast<double>(image.image->height)}));
    }
    if (bounds.empty()) {
        bounds.add({0.0, 0.0});
        bounds.add({10.0, 10.0});
        return bounds;
    }
    const double margin = std::max(0.5, 0.04 * std::max(bounds.maxX - bounds.minX, bounds.maxY - bounds.minY));
    bounds.minX -= margin;
    bounds.minY -= margin;
    bounds.maxX += margin;
    bounds.maxY += margin;
    return bounds;
}

// A path in lower case with forward slashes, for comparing paths the way Windows does.
std::wstring folded(const fs::path& path)
{
    std::wstring text = path.lexically_normal().generic_wstring();
    for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
    while (text.size() > 1 && text.back() == L'/') text.pop_back();
    return text;
}

// Says whether path is folder itself or somewhere below it.
bool isInside(const fs::path& path, const fs::path& folder)
{
    const std::wstring p = folded(path);
    const std::wstring f = folded(folder);
    return p == f || (p.size() > f.size() && p.compare(0, f.size(), f) == 0 && p[f.size()] == L'/');
}

// Makes a warning of the plan picture.
BuildProblem warning(std::string code, std::string message, const Id& floor)
{
    BuildProblem problem;
    problem.code = std::move(code);
    problem.severity = BuildSeverity::Warning;
    problem.message = std::move(message);
    problem.floor = floor;
    problem.step = "render";
    return problem;
}

}

// The three bytes of one pixel.
std::array<std::uint8_t, 3> RgbImage::at(int x, int y) const
{
    const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 3;
    return {pixels[i], pixels[i + 1], pixels[i + 2]};
}

// Turns a plan position into picture pixels.
Vec2 planToPicture(const PlanPicture& picture, Vec2 position)
{
    return {(position.x - picture.origin.x) * picture.pixelsPerMetre, (position.y - picture.origin.y) * picture.pixelsPerMetre};
}

// Works out the area and the size, then draws the layers from the bottom up.
PlanPicture drawPlanPicture(const Plan& plan, const Floor& floor, const GrayImage* background,
                            const PlanPictureOptions& options)
{
    PlanPicture picture;
    if (options.longSide < kMinPlanPictureSide || options.longSide > kMaxPlanPictureSide) {
        picture.status = fail(ErrorCode::InvalidValue, "The picture size must be " + std::to_string(kMinPlanPictureSide) +
                                                           " to " + std::to_string(kMaxPlanPictureSide) +
                                                           " pixels on the long side, not " +
                                                           std::to_string(options.longSide) + ".");
        return picture;
    }
    ImageArea image;
    const bool hasScale = plan.scale.pixelsPerMetre && std::isfinite(*plan.scale.pixelsPerMetre) && *plan.scale.pixelsPerMetre > 0.0;
    if (options.image && hasScale && background != nullptr && background->width > 0 && background->height > 0 &&
        background->pixels.size() == static_cast<std::size_t>(background->width) * static_cast<std::size_t>(background->height)) {
        image.image = background;
        image.pixelsPerMetre = *plan.scale.pixelsPerMetre;
    }

    Bounds area = pictureArea(plan, floor, image);
    double width = area.maxX - area.minX;
    double height = area.maxY - area.minY;
    const double scale = options.longSide / std::max(width, height);
    const double shortest = kMinPlanPictureSide / scale;
    if (width < shortest) {
        area.minX -= (shortest - width) / 2.0;
        width = shortest;
    }
    if (height < shortest) {
        area.minY -= (shortest - height) / 2.0;
        height = shortest;
    }
    const int pixelsWide = std::clamp(static_cast<int>(std::lround(width * scale)), kMinPlanPictureSide, options.longSide);
    const int pixelsHigh = std::clamp(static_cast<int>(std::lround(height * scale)), kMinPlanPictureSide, options.longSide);

    Canvas canvas;
    canvas.origin = {area.minX, area.minY};
    canvas.scale = scale;
    canvas.stroke = std::clamp(static_cast<int>(std::lround(options.longSide / 1000.0)), 1, 4);
    canvas.fontScale = std::clamp(options.longSide / 2000.0 * 0.6, 0.35, 1.3);
    canvas.fontThickness = canvas.fontScale >= 0.9 ? 2 : 1;
    canvas.dot = std::max(2, static_cast<int>(std::lround(options.longSide / 450.0)));

    try {
        canvas.rgb = cv::Mat(pixelsHigh, pixelsWide, CV_8UC3, scalar(kRaised));
        if (image.image != nullptr) {
            drawImage(canvas, floor, image);
            picture.imageShown = true;
        }
        if (options.grid) drawGrid(canvas);

        cv::Mat rooms = cv::Mat::zeros(canvas.rgb.size(), CV_8UC1);
        for (const Room& room : floor.rooms) {
            const RoomShape shape = roomShape(floor, room);
            if (!shape.closed) continue;
            std::vector<std::vector<cv::Point>> rings;
            for (const auto& ring : shape.floorArea) {
                if (ring.size() >= 3) rings.push_back(canvas.fineRing(ring));
            }
            if (!rings.empty()) cv::fillPoly(rooms, rings, cv::Scalar(255), cv::LINE_AA, kShift);
        }
        blendMask(canvas.rgb, rooms, kSignalBlue, kRoomFillAlpha);

        cv::Mat walls = cv::Mat::zeros(canvas.rgb.size(), CV_8UC1);
        for (const Wall& wall : floor.walls) {
            for (const auto& ring : wallOutline(floor, wall)) {
                if (ring.size() < 3) continue;
                const std::vector<std::vector<cv::Point>> rings = {canvas.fineRing(ring)};
                cv::fillPoly(walls, rings, cv::Scalar(255), cv::LINE_AA, kShift);
            }
        }
        for (const Opening& opening : floor.openings) {
            const OpeningPlacement place = openingPlacement(floor, opening);
            if (!place.valid || place.footprint.size() < 3) continue;
            const std::vector<std::vector<cv::Point>> rings = {canvas.fineRing(place.footprint)};
            cv::fillPoly(walls, rings, cv::Scalar(0), cv::LINE_AA, kShift);
        }
        blendMask(canvas.rgb, walls, kSignalBlue, kOverlayAlpha);

        drawOpeningMarks(canvas, floor);
        drawStairs(canvas, plan, floor);
        drawItems(canvas, floor);
        drawLights(canvas, floor);
        drawPoints(canvas, floor);

        Labeller labeller(canvas);
        if (options.grid) drawScaleBar(canvas, labeller);
        if (options.labels) {
            std::string title = printable(floor.id);
            if (!floor.name.empty()) title += " " + printable(floor.name);
            labeller.add(floor.id, "floor", {title, fixed(scale, 1) + " px per m"}, {0.0, 0.0}, false, false, picture.labels);
            drawLabels(canvas, labeller, floor, picture.labels);
        }
    } catch (const std::exception& error) {
        picture = PlanPicture{};
        picture.status = fail(ErrorCode::InvalidValue, std::string("The plan picture could not be drawn: ") + error.what());
        return picture;
    }

    picture.pixelsPerMetre = scale;
    picture.origin = canvas.origin;
    picture.image.width = canvas.rgb.cols;
    picture.image.height = canvas.rgb.rows;
    picture.image.pixels.assign(canvas.rgb.datastart, canvas.rgb.dataend);
    return picture;
}

// Checks the floor and the output, loads the image, draws and writes the PNG.
PlanRenderReport renderPlanPicture(const fs::path& folder, const Project& project, const PlanRenderOptions& options)
{
    PlanRenderReport report;
    std::error_code ec;
    fs::path root = fs::absolute(folder, ec).lexically_normal();
    if (ec) root = folder.lexically_normal();
    if (!isProjectFolder(root)) {
        report.status = fail(ErrorCode::NotAProject, "The folder " + pathToUtf8(root) + " holds no project.");
        return report;
    }
    const Plan& plan = project.plan;
    const Floor* floor = options.floor.empty() ? (plan.floors.empty() ? nullptr : &plan.floors.front())
                                               : findFloor(plan, options.floor);
    if (floor == nullptr) {
        report.status = fail(ErrorCode::NotFound, options.floor.empty() ? std::string("The plan has no floor.")
                                                                         : "The plan has no floor " + options.floor + ".");
        return report;
    }
    report.floor = floor->id;

    fs::path out = options.out;
    if (out.empty()) out = fs::path(pathFromUtf8(kRendersFolderName)) / pathFromUtf8("plan-" + floor->id + ".png");
    if (out.is_relative()) out = root / out;
    out = out.lexically_normal();
    std::wstring extension = out.extension().wstring();
    for (wchar_t& c : extension) c = static_cast<wchar_t>(std::towlower(c));
    if (out.filename().empty() || out.stem().empty() || extension != L".png") {
        report.status = fail(ErrorCode::InvalidValue, "The picture file must end in .png: " + pathToUtf8(out) + ".");
        return report;
    }
    for (std::wstring_view guarded : kGuardedFolders) {
        if (isInside(out, root / fs::path(guarded))) {
            report.status = fail(ErrorCode::InvalidValue, "A picture may not be written into the project's " +
                                                              pathToUtf8(fs::path(guarded)) + " folder. Use renders/.");
            return report;
        }
    }
    if (fs::is_directory(out, ec)) {
        report.status = fail(ErrorCode::InvalidValue, pathToUtf8(out) + " is a folder, not a picture file.");
        return report;
    }

    GrayImageLoad loaded;
    if (options.picture.image && floor->image && !floor->image->empty()) {
        if (!plan.scale.pixelsPerMetre) {
            report.problems.push_back(warning("no_scale", "The plan has no scale yet, so the plan image is not drawn. Set it "
                                                          "with plynth scale set.", floor->id));
        } else {
            loaded = loadGrayImage(resolveImagePath(root, *floor->image));
            if (!loaded.status.ok()) {
                report.problems.push_back(warning("image_missing", "The plan image of floor " + floor->id +
                                                                       " is not drawn: " + loaded.status.message,
                                                  floor->id));
            }
        }
    }

    PlanPicture picture = drawPlanPicture(plan, *floor, loaded.image.get(), options.picture);
    if (!picture.status.ok()) {
        report.status = picture.status;
        return report;
    }

    std::vector<std::uint8_t> png;
    try {
        cv::Mat rgb(picture.image.height, picture.image.width, CV_8UC3, picture.image.pixels.data());
        cv::Mat bgr;
        cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
        if (!cv::imencode(".png", bgr, png)) png.clear();
    } catch (const std::exception&) {
        png.clear();
    }
    if (png.empty()) {
        report.status = fail(ErrorCode::CannotWrite, "The plan picture could not be turned into a PNG.");
        return report;
    }

    fs::create_directories(out.parent_path(), ec);
    if (!fs::is_directory(out.parent_path(), ec)) {
        report.status = fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(out.parent_path()) + " could not be created.");
        return report;
    }
    if (Status written = fileio::writeAtomic(out, std::string(png.begin(), png.end())); !written.ok()) {
        report.status = written;
        return report;
    }

    report.file = out;
    report.width = picture.image.width;
    report.height = picture.image.height;
    report.pixelsPerMetre = picture.pixelsPerMetre;
    report.origin = picture.origin;
    report.imageShown = picture.imageShown;
    report.labels = std::move(picture.labels);
    return report;
}

// The result object of a plan picture.
Json planRenderReportToJson(const PlanRenderReport& report)
{
    Json json = Json::object();
    json["file"] = pathToUtf8(report.file);
    json["width"] = report.width;
    json["height"] = report.height;
    json["scale"] = std::round(report.pixelsPerMetre * 1000.0) / 1000.0;
    json["origin"] = Json{{"x", std::round(report.origin.x * 1000.0) / 1000.0}, {"y", std::round(report.origin.y * 1000.0) / 1000.0}};
    json["floor"] = report.floor;
    json["image"] = report.imageShown;
    Json labels = Json::array();
    for (const PlanLabel& label : report.labels) {
        labels.push_back(Json{{"id", label.id},
                              {"kind", label.kind},
                              {"x", label.x},
                              {"y", label.y},
                              {"width", label.width},
                              {"height", label.height}});
    }
    json["labels"] = std::move(labels);
    return json;
}

}
