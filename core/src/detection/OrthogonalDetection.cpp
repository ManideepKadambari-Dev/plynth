#include "OrthogonalDetection.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace plynth {

namespace {

constexpr double kMaxSlopeDegrees = 4.0;   // bands tilted more than this are not walls
constexpr double kExactDegrees = 0.5;      // walls closer than this to straight are made exactly straight
constexpr double kJoinSlack = 2.0;         // pixels added to half the other wall's thickness when joining
constexpr double kMinCleanSides = 0.5;     // share of a band whose two sides must be free of other ink

// Pixel limits worked out from the settings.
struct Limits {
    double minThickness = 0.0;
    double maxThickness = 0.0;
    int minLength = 0;
    double maxGap = 0.0;
    double minGap = 0.0;
};

// One row (or column) of a band: where it sits along the wall and which cross positions it covers.
struct Station {
    int along = 0;
    int p0 = 0;
    int p1 = 0; // one past the last pixel
};

struct Band {
    std::vector<Station> stations;
};

// A straight wall piece in pixels. axis 0 runs along x (horizontal), axis 1 along y (vertical). The centre
// line goes from (a0, c0) to (a1, c1) in (along, cross) terms.
struct Piece {
    int axis = 0;
    double a0 = 0.0;
    double a1 = 0.0;
    double c0 = 0.0;
    double c1 = 0.0;
    double thickness = 0.0;
    bool flat = false;
    std::vector<std::pair<double, double>> gaps; // closed gaps as along ranges

    double slope() const { return a1 > a0 ? (c1 - c0) / (a1 - a0) : 0.0; }
    double crossAt(double a) const { return c0 + slope() * (a - a0); }
    Vec2 at(double a) const
    {
        const double c = crossAt(a);
        return axis == 0 ? Vec2{a, c} : Vec2{c, a};
    }
};

// Picks the grey level below which a pixel counts as ink: Otsu's threshold scaled by the sensitivity.
int inkLevel(const GrayImage& image, double sensitivity)
{
    std::array<std::uint64_t, 256> hist{};
    for (std::uint8_t v : image.pixels) ++hist[v];
    const double total = static_cast<double>(image.pixels.size());
    double sumAll = 0.0;
    for (int i = 0; i < 256; ++i) sumAll += i * static_cast<double>(hist[static_cast<std::size_t>(i)]);

    std::array<double, 256> between{};
    double best = -1.0;
    double weightBack = 0.0;
    double sumBack = 0.0;
    for (int t = 0; t < 256; ++t) {
        weightBack += static_cast<double>(hist[static_cast<std::size_t>(t)]);
        sumBack += t * static_cast<double>(hist[static_cast<std::size_t>(t)]);
        const double weightFront = total - weightBack;
        double v = 0.0;
        if (weightBack > 0.0 && weightFront > 0.0) {
            const double diff = sumBack / weightBack - (sumAll - sumBack) / weightFront;
            v = weightBack * weightFront * diff * diff;
        }
        between[static_cast<std::size_t>(t)] = v;
        best = std::max(best, v);
    }
    int first = -1;
    int last = -1;
    for (int t = 0; t < 256; ++t) {
        if (between[static_cast<std::size_t>(t)] >= best * (1.0 - 1e-9)) {
            if (first < 0) first = t;
            last = t;
        }
    }
    const double middle = (first + last) / 2.0;
    const double level = std::round((middle + 1.0) * (0.5 + sensitivity));
    return static_cast<int>(std::clamp(level, 1.0, 254.0));
}

// Marks every pixel of the wall mask that lies in a horizontal run of at least minRun pixels.
cv::Mat horizontalRuns(const cv::Mat& wall, int minRun)
{
    cv::Mat out = cv::Mat::zeros(wall.size(), CV_8U);
    for (int y = 0; y < wall.rows; ++y) {
        const std::uint8_t* row = wall.ptr<std::uint8_t>(y);
        std::uint8_t* dst = out.ptr<std::uint8_t>(y);
        int x = 0;
        while (x < wall.cols) {
            while (x < wall.cols && row[x] == 0) ++x;
            const int start = x;
            while (x < wall.cols && row[x] != 0) ++x;
            if (x - start >= minRun) std::fill(dst + start, dst + x, std::uint8_t{255});
        }
    }
    return out;
}

// Marks every pixel of the wall mask that lies in a vertical run of at least minRun pixels.
cv::Mat verticalRuns(const cv::Mat& wall, int minRun)
{
    const int w = wall.cols;
    const int h = wall.rows;
    cv::Mat out = cv::Mat::zeros(wall.size(), CV_8U);
    std::vector<std::uint32_t> above(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    std::vector<std::uint32_t> count(static_cast<std::size_t>(w), 0);
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* row = wall.ptr<std::uint8_t>(y);
        std::uint32_t* dst = above.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
        for (int x = 0; x < w; ++x) {
            count[static_cast<std::size_t>(x)] = row[x] ? count[static_cast<std::size_t>(x)] + 1 : 0u;
            dst[x] = count[static_cast<std::size_t>(x)];
        }
    }
    std::fill(count.begin(), count.end(), 0u);
    for (int y = h - 1; y >= 0; --y) {
        const std::uint8_t* row = wall.ptr<std::uint8_t>(y);
        std::uint8_t* o = out.ptr<std::uint8_t>(y);
        const std::uint32_t* up = above.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
        for (int x = 0; x < w; ++x) {
            std::uint32_t& c = count[static_cast<std::size_t>(x)];
            c = row[x] ? c + 1 : 0u;
            if (row[x] && up[x] + c - 1 >= static_cast<std::uint32_t>(minRun)) o[x] = 255;
        }
    }
    return out;
}

// Follows the runs of a mask row by row and groups runs of similar thickness that touch the row before
// into bands. Rows are the direction along the walls.
std::vector<Band> traceBands(const cv::Mat& m, const std::stop_token& stop, bool& stopped)
{
    struct Run {
        int p0;
        int p1;
        int band;
    };
    std::vector<Band> bands;
    std::vector<Run> prev;
    std::vector<Run> cur;
    std::vector<char> used;
    for (int r = 0; r < m.rows; ++r) {
        if ((r & 255) == 0 && stop.stop_requested()) {
            stopped = true;
            return {};
        }
        const std::uint8_t* row = m.ptr<std::uint8_t>(r);
        cur.clear();
        int x = 0;
        while (x < m.cols) {
            while (x < m.cols && row[x] == 0) ++x;
            if (x >= m.cols) break;
            const int start = x;
            while (x < m.cols && row[x] != 0) ++x;
            cur.push_back({start, x, -1});
        }
        used.assign(prev.size(), 0);
        std::size_t j = 0;
        for (Run& c : cur) {
            while (j < prev.size() && prev[j].p1 <= c.p0) ++j;
            int best = -1;
            int bestOverlap = 0;
            const int t1 = c.p1 - c.p0;
            for (std::size_t k = j; k < prev.size() && prev[k].p0 < c.p1; ++k) {
                if (used[k]) continue;
                const int t2 = prev[k].p1 - prev[k].p0;
                if (std::abs(t1 - t2) > std::max(3.0, 0.3 * t2)) continue;
                const int overlap = std::min(c.p1, prev[k].p1) - std::max(c.p0, prev[k].p0);
                if (overlap > bestOverlap) {
                    bestOverlap = overlap;
                    best = static_cast<int>(k);
                }
            }
            if (best >= 0) {
                used[static_cast<std::size_t>(best)] = 1;
                c.band = prev[static_cast<std::size_t>(best)].band;
            } else {
                c.band = static_cast<int>(bands.size());
                bands.emplace_back();
            }
            bands[static_cast<std::size_t>(c.band)].stations.push_back({r, c.p0, c.p1});
        }
        prev.swap(cur);
    }
    return bands;
}

// Says whether the wall mask has ink at a pixel, where cross runs across the wall and along runs with it.
bool wallInk(const cv::Mat& wall, int axis, int along, int cross)
{
    const int x = axis == 0 ? along : cross;
    const int y = axis == 0 ? cross : along;
    if (x < 0 || y < 0 || x >= wall.cols || y >= wall.rows) return false;
    return wall.at<std::uint8_t>(y, x) != 0;
}

// Turns a band into a wall piece, or nothing when it is too short, too thin or thick, bent, tilted or has
// other ink along its sides.
std::optional<Piece> makePiece(const Band& band, int axis, const cv::Mat& wall, const Limits& lim)
{
    const std::size_t n = band.stations.size();
    if (static_cast<int>(n) < lim.minLength) return std::nullopt;

    std::vector<int> widths(n);
    for (std::size_t i = 0; i < n; ++i) widths[i] = band.stations[i].p1 - band.stations[i].p0;
    std::vector<int> sorted = widths;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(n / 2), sorted.end());
    const double thickness = sorted[n / 2];
    if (thickness < lim.minThickness - 1.0 || thickness > lim.maxThickness + 1.0) return std::nullopt;

    double sumA = 0.0;
    double sumC = 0.0;
    for (const Station& s : band.stations) {
        sumA += s.along + 0.5;
        sumC += (s.p0 + s.p1) / 2.0;
    }
    const double meanA = sumA / static_cast<double>(n);
    const double meanC = sumC / static_cast<double>(n);
    double saa = 0.0;
    double sac = 0.0;
    for (const Station& s : band.stations) {
        const double da = s.along + 0.5 - meanA;
        saa += da * da;
        sac += da * ((s.p0 + s.p1) / 2.0 - meanC);
    }
    const double slope = saa > 0.0 ? sac / saa : 0.0;
    if (std::abs(slope) > std::tan(kMaxSlopeDegrees * std::numbers::pi / 180.0)) return std::nullopt;

    double worst = 0.0;
    int dirty = 0;
    for (const Station& s : band.stations) {
        const double fit = meanC + slope * (s.along + 0.5 - meanA);
        worst = std::max(worst, std::abs((s.p0 + s.p1) / 2.0 - fit));
        if (wallInk(wall, axis, s.along, s.p0 - 2) || wallInk(wall, axis, s.along, s.p1 + 1)) ++dirty;
    }
    if (worst > std::max(2.0, 0.25 * thickness)) return std::nullopt;
    if (static_cast<double>(n - static_cast<std::size_t>(dirty)) < kMinCleanSides * static_cast<double>(n)) return std::nullopt;

    Piece p;
    p.axis = axis;
    p.a0 = band.stations.front().along;
    p.a1 = band.stations.back().along + 1.0;
    p.thickness = thickness;
    p.flat = std::abs(slope) <= std::tan(kExactDegrees * std::numbers::pi / 180.0);
    if (p.flat) {
        p.c0 = meanC;
        p.c1 = meanC;
    } else {
        p.c0 = meanC + slope * (p.a0 - meanA);
        p.c1 = meanC + slope * (p.a1 - meanA);
    }
    return p;
}

// Joins pieces that lie in line with a short gap between them into one wall and remembers each gap.
std::vector<Piece> closeGaps(const std::vector<Piece>& pieces, const Limits& lim)
{
    struct Link {
        double gap;
        std::size_t i;
        std::size_t j;
    };
    std::vector<Link> links;
    for (int axis = 0; axis < 2; ++axis) {
        std::vector<std::size_t> idx;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            if (pieces[i].axis == axis) idx.push_back(i);
        }
        std::sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) { return pieces[a].a0 < pieces[b].a0; });
        for (std::size_t x = 0; x < idx.size(); ++x) {
            const Piece& p = pieces[idx[x]];
            for (std::size_t y = 0; y < idx.size(); ++y) {
                const Piece& q = pieces[idx[y]];
                if (q.a0 < p.a1 - 1e-9 || idx[x] == idx[y]) continue;
                const double gap = q.a0 - p.a1;
                if (gap > lim.maxGap) break;
                const double mid = (p.a1 + q.a0) / 2.0;
                if (std::abs(p.crossAt(mid) - q.crossAt(mid)) > 0.5 * std::min(p.thickness, q.thickness)) continue;
                if (std::abs(p.thickness - q.thickness) > std::max(1.5, 0.2 * std::max(p.thickness, q.thickness))) continue;
                links.push_back({gap, idx[x], idx[y]});
            }
        }
    }
    std::sort(links.begin(), links.end(), [](const Link& a, const Link& b) {
        if (a.gap != b.gap) return a.gap < b.gap;
        if (a.i != b.i) return a.i < b.i;
        return a.j < b.j;
    });
    constexpr std::size_t none = static_cast<std::size_t>(-1);
    std::vector<std::size_t> next(pieces.size(), none);
    std::vector<std::size_t> before(pieces.size(), none);
    for (const Link& l : links) {
        if (next[l.i] != none || before[l.j] != none) continue;
        next[l.i] = l.j;
        before[l.j] = l.i;
    }

    std::vector<Piece> walls;
    for (std::size_t head = 0; head < pieces.size(); ++head) {
        if (before[head] != none) continue;
        Piece w = pieces[head];
        double weight = w.a1 - w.a0;
        double thicknessSum = w.thickness * weight;
        double crossSum = w.c0 * weight;
        bool allFlat = w.flat;
        std::size_t at = head;
        while (next[at] != none) {
            const Piece& a = pieces[at];
            const Piece& b = pieces[next[at]];
            if (b.a0 - a.a1 >= lim.minGap) w.gaps.push_back({a.a1, b.a0});
            const double len = b.a1 - b.a0;
            weight += len;
            thicknessSum += b.thickness * len;
            crossSum += b.c0 * len;
            allFlat = allFlat && b.flat;
            w.a1 = b.a1;
            w.c1 = b.c1;
            at = next[at];
        }
        w.thickness = thicknessSum / weight;
        if (allFlat) {
            w.flat = true;
            w.c0 = crossSum / weight;
            w.c1 = w.c0;
        } else {
            w.flat = false;
        }
        walls.push_back(std::move(w));
    }
    return walls;
}

// A place where another wall meets this one.
struct Join {
    double param; // along this wall
    Vec2 at;
    double reach; // how far past an end of this wall the join still counts as being at that end
};

// A point on a wall in the order along it.
struct WallPoint {
    double param;
    std::size_t raw;
};

// Works out where the centre lines of a horizontal and a vertical wall cross.
Vec2 crossing(const Piece& h, const Piece& v)
{
    const double sh = h.slope();
    const double sv = v.slope();
    const double x = (v.c0 + sv * (h.c0 - sh * h.a0 - v.a0)) / (1.0 - sh * sv);
    return {x, h.c0 + sh * (x - h.a0)};
}

// Tells whether thin ink forms a quarter circle of the given radius around a hinge.
bool hasArc(const cv::Mat& thin, const Piece& wall, double hingeAlong, double direction, double side, double radius)
{
    int hits = 0;
    int samples = 0;
    for (int deg = 20; deg <= 70; deg += 10) {
        const double theta = deg * std::numbers::pi / 180.0;
        const double along = hingeAlong + direction * radius * std::sin(theta);
        const double cross = wall.crossAt(along) + side * radius * std::cos(theta);
        const double x = wall.axis == 0 ? along : cross;
        const double y = wall.axis == 0 ? cross : along;
        ++samples;
        bool hit = false;
        for (int dy = -3; dy <= 3 && !hit; ++dy) {
            for (int dx = -3; dx <= 3 && !hit; ++dx) {
                if (dx * dx + dy * dy > 10) continue;
                const int px = static_cast<int>(std::floor(x)) + dx;
                const int py = static_cast<int>(std::floor(y)) + dy;
                if (px < 0 || py < 0 || px >= thin.cols || py >= thin.rows) continue;
                if (thin.at<std::uint8_t>(py, px) != 0) hit = true;
            }
        }
        if (hit) ++hits;
    }
    return hits >= samples - 1;
}

// Decides whether a closed gap is a window (two or more thin lines inside the wall), a door (a quarter arc
// starting at one end) or a plain opening.
OpeningType classifyGap(const cv::Mat& thin, const Piece& wall, double g0, double g1)
{
    const double width = g1 - g0;
    const double from = g0 + 0.15 * width;
    const double to = g1 - 0.15 * width;
    const int along = std::max(4, static_cast<int>((to - from) / 2.0));
    int groups = 0;
    bool inLine = false;
    for (double o = -wall.thickness / 2.0 + 1.0; o <= wall.thickness / 2.0 - 1.0 + 1e-9; o += 1.0) {
        int hits = 0;
        for (int i = 0; i < along; ++i) {
            const double a = from + (to - from) * (i + 0.5) / along;
            const double c = wall.crossAt(a) + o;
            const int x = static_cast<int>(std::floor(wall.axis == 0 ? a : c));
            const int y = static_cast<int>(std::floor(wall.axis == 0 ? c : a));
            if (x >= 0 && y >= 0 && x < thin.cols && y < thin.rows && thin.at<std::uint8_t>(y, x) != 0) ++hits;
        }
        const bool line = hits >= 0.7 * along;
        if (line && !inLine) ++groups;
        inLine = line;
    }
    if (groups >= 2) return OpeningType::Window;

    for (double scale : {1.0, 0.96, 1.04}) {
        const double radius = width * scale;
        for (double side : {1.0, -1.0}) {
            if (hasArc(thin, wall, g0, 1.0, side, radius) || hasArc(thin, wall, g1, -1.0, side, radius)) {
                return OpeningType::Door;
            }
        }
    }
    return OpeningType::Plain;
}

// Rounds a length in metres to a whole centimetre, at least one.
double roundThickness(double metres)
{
    return std::max(0.01, std::round(metres * 100.0) / 100.0);
}

}

// Runs all the steps from the grey image to points, walls and openings.
DetectionResult detectOrthogonal(const GrayImage& image, double pixelsPerMetre, const DetectionParams& params,
                                 std::stop_token stop)
{
    DetectionResult result;
    result.imageWidth = image.width;
    result.imageHeight = image.height;
    const auto cancelled = [&]() {
        result = DetectionResult{};
        result.imageWidth = image.width;
        result.imageHeight = image.height;
        result.cancelled = true;
        return result;
    };
    if (stop.stop_requested()) return cancelled();

    const double ppm = pixelsPerMetre;
    Limits lim;
    lim.minThickness = params.minThickness * ppm;
    lim.maxThickness = params.maxThickness * ppm;
    lim.minLength = std::max(2, static_cast<int>(std::ceil(params.minLength * ppm)));
    lim.maxGap = params.maxOpeningWidth * ppm;
    lim.minGap = std::max(4.0, lim.minThickness);

    // Black and white, then erase everything thinner than the thinnest wall.
    cv::Mat gray(image.height, image.width, CV_8UC1, const_cast<std::uint8_t*>(image.pixels.data()));
    const int level = inkLevel(image, params.sensitivity);
    cv::Mat ink;
    cv::compare(gray, level, ink, cv::CMP_LT);
    if (stop.stop_requested()) return cancelled();

    int kernel = std::max(3, static_cast<int>(std::lround(lim.minThickness)));
    if (kernel % 2 == 0) --kernel;
    kernel = std::max(3, kernel);
    cv::Mat wall;
    cv::morphologyEx(ink, wall, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(kernel, kernel)));
    if (stop.stop_requested()) return cancelled();

    // Split the wall mask into horizontal and vertical runs, so the thickness of a crossing wall never
    // passes as a run.
    const int runLength = std::max(lim.minLength, static_cast<int>(std::floor(1.5 * lim.maxThickness)) + 1);
    cv::Mat horizontal = horizontalRuns(wall, runLength);
    cv::Mat horizontalAcross;
    cv::transpose(horizontal, horizontalAcross);
    horizontal.release();
    if (stop.stop_requested()) return cancelled();
    cv::Mat vertical = verticalRuns(wall, runLength);
    if (stop.stop_requested()) return cancelled();

    // Measure the bands, and keep the straight ones of wall thickness.
    bool stopped = false;
    std::vector<Piece> pieces;
    for (const Band& b : traceBands(horizontalAcross, stop, stopped)) {
        if (auto p = makePiece(b, 0, wall, lim)) pieces.push_back(*p);
    }
    if (stopped) return cancelled();
    for (const Band& b : traceBands(vertical, stop, stopped)) {
        if (auto p = makePiece(b, 1, wall, lim)) pieces.push_back(*p);
    }
    if (stopped || stop.stop_requested()) return cancelled();
    horizontalAcross.release();
    vertical.release();

    std::vector<Piece> walls = closeGaps(pieces, lim);

    // Join walls at corners and junctions.
    std::vector<std::vector<Join>> joins(walls.size());
    for (std::size_t i = 0; i < walls.size(); ++i) {
        if (walls[i].axis != 0) continue;
        for (std::size_t j = 0; j < walls.size(); ++j) {
            if (walls[j].axis != 1) continue;
            const Piece& h = walls[i];
            const Piece& v = walls[j];
            const Vec2 x = crossing(h, v);
            const double reachH = v.thickness / 2.0 + kJoinSlack;
            const double reachV = h.thickness / 2.0 + kJoinSlack;
            if (x.x < h.a0 - reachH || x.x > h.a1 + reachH || x.y < v.a0 - reachV || x.y > v.a1 + reachV) continue;
            joins[i].push_back({x.x, x, reachH});
            joins[j].push_back({x.y, x, reachV});
        }
    }
    if (stop.stop_requested()) return cancelled();

    std::vector<Vec2> raw;
    std::vector<std::vector<WallPoint>> sequence(walls.size());
    for (std::size_t w = 0; w < walls.size(); ++w) {
        const Piece& p = walls[w];
        std::vector<Join>& js = joins[w];
        int startJoin = -1;
        int endJoin = -1;
        for (std::size_t k = 0; k < js.size(); ++k) {
            if (js[k].param <= p.a0 + js[k].reach &&
                (startJoin < 0 || std::abs(js[k].param - p.a0) < std::abs(js[static_cast<std::size_t>(startJoin)].param - p.a0))) {
                startJoin = static_cast<int>(k);
            }
            if (js[k].param >= p.a1 - js[k].reach &&
                (endJoin < 0 || std::abs(js[k].param - p.a1) < std::abs(js[static_cast<std::size_t>(endJoin)].param - p.a1))) {
                endJoin = static_cast<int>(k);
            }
        }
        if (startJoin >= 0 && startJoin == endJoin) endJoin = -1;
        std::vector<std::pair<double, Vec2>> pts;
        pts.push_back(startJoin >= 0 ? std::pair{js[static_cast<std::size_t>(startJoin)].param, js[static_cast<std::size_t>(startJoin)].at}
                                     : std::pair{p.a0, p.at(p.a0)});
        for (std::size_t k = 0; k < js.size(); ++k) {
            if (static_cast<int>(k) == startJoin || static_cast<int>(k) == endJoin) continue;
            pts.push_back({js[k].param, js[k].at});
        }
        pts.push_back(endJoin >= 0 ? std::pair{js[static_cast<std::size_t>(endJoin)].param, js[static_cast<std::size_t>(endJoin)].at}
                                   : std::pair{p.a1, p.at(p.a1)});
        std::stable_sort(pts.begin() + 1, pts.end() - 1, [](const auto& a, const auto& b) { return a.first < b.first; });
        if (pts.back().first - pts.front().first < 2.0) continue;
        for (const auto& [param, pos] : pts) {
            sequence[w].push_back({param, raw.size()});
            raw.push_back(pos);
        }
    }

    // Points within a few pixels of each other become one point. The radius is at least 1.5 mm so merged
    // points always stay the 1 mm apart that a valid result needs.
    const double radius = std::max(3.0, 0.0015 * ppm);
    std::vector<std::size_t> parent(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) parent[i] = i;
    const auto find = [&](std::size_t a) {
        while (parent[a] != a) {
            parent[a] = parent[parent[a]];
            a = parent[a];
        }
        return a;
    };
    std::vector<std::size_t> order(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return raw[a].x < raw[b].x; });
    for (std::size_t a = 0; a < order.size(); ++a) {
        for (std::size_t b = a + 1; b < order.size() && raw[order[b]].x - raw[order[a]].x <= radius; ++b) {
            if (distance(raw[order[a]], raw[order[b]]) <= radius) parent[find(order[a])] = find(order[b]);
        }
    }
    std::vector<int> pointOf(raw.size(), -1);
    std::vector<Vec2> centre;
    std::vector<int> members;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const std::size_t root = find(i);
        if (pointOf[root] < 0) {
            pointOf[root] = static_cast<int>(centre.size());
            centre.push_back({0.0, 0.0});
            members.push_back(0);
        }
        const std::size_t id = static_cast<std::size_t>(pointOf[root]);
        centre[id].x += raw[i].x;
        centre[id].y += raw[i].y;
        ++members[id];
    }
    for (std::size_t i = 0; i < centre.size(); ++i) {
        centre[i].x /= members[i];
        centre[i].y /= members[i];
    }
    const auto pointId = [&](std::size_t rawIndex) { return static_cast<std::size_t>(pointOf[find(rawIndex)]); };

    // Split every wall at its points and keep one wall between two points.
    struct Sub {
        std::size_t wall;
        double from;
        double to;
        std::size_t index;
    };
    std::vector<Sub> subs;
    std::set<std::pair<std::size_t, std::size_t>> pairs;
    for (std::size_t w = 0; w < walls.size(); ++w) {
        const std::vector<WallPoint>& seq = sequence[w];
        if (seq.size() < 2) continue;
        std::size_t last = 0;
        for (std::size_t k = 1; k < seq.size(); ++k) {
            const std::size_t a = pointId(seq[last].raw);
            const std::size_t b = pointId(seq[k].raw);
            if (a == b) continue;
            const auto key = std::pair{std::min(a, b), std::max(a, b)};
            if (pairs.insert(key).second) {
                ProposedWall pw;
                pw.from = a;
                pw.to = b;
                pw.thickness = roundThickness(walls[w].thickness / ppm);
                subs.push_back({w, seq[last].param, seq[k].param, result.walls.size()});
                result.walls.push_back(pw);
            }
            last = k;
        }
    }
    for (const Vec2& c : centre) result.points.push_back({c.x / ppm, c.y / ppm});
    if (stop.stop_requested()) return cancelled();

    // Openings in the closed gaps, seen in the ink the wall mask left out.
    if (params.openings) {
        cv::Mat thin;
        cv::bitwise_and(ink, ~wall, thin);
        for (std::size_t w = 0; w < walls.size(); ++w) {
            for (const auto& [g0, g1] : walls[w].gaps) {
                const double mid = (g0 + g1) / 2.0;
                for (const Sub& s : subs) {
                    if (s.wall != w || mid < s.from || mid > s.to) continue;
                    const ProposedWall& pw = result.walls[s.index];
                    const Vec2 from{centre[pw.from].x, centre[pw.from].y};
                    const Vec2 to{centre[pw.to].x, centre[pw.to].y};
                    const double length = distance(from, to);
                    const Vec2 at = walls[w].at(mid);
                    const double offset = length > 0.0
                        ? std::clamp(((at.x - from.x) * (to.x - from.x) + (at.y - from.y) * (to.y - from.y)) / length, 0.0, length)
                        : 0.0;
                    ProposedOpening o;
                    o.wall = s.index;
                    o.type = classifyGap(thin, walls[w], g0, g1);
                    o.offset = offset / ppm;
                    o.width = (g1 - g0) / ppm;
                    result.openings.push_back(o);
                    break;
                }
            }
        }
    }
    return result;
}

}
