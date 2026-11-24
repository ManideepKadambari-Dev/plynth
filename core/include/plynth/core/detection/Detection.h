#pragma once

#include "../Geometry.h"
#include "../Plan.h"
#include "../Status.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// Detection finds walls, rooms and openings in a plan image and returns them as proposals. Proposals are
// not part of the plan: they are shown, listed and then accepted with ApplyDetectionCommand, which turns
// them into ordinary points, walls, openings and rooms. All lengths are in metres. A detected result measures
// positions from the image's top left corner, so pixel (px, py) is at (px / pixelsPerMetre, py / pixelsPerMetre).
// proposalsOnPlan gives the same proposals in plan coordinates (see Plan.h): use that copy to compare, show
// and list, and give the detected result to ApplyDetectionCommand.

inline constexpr double kDefaultMinWallThickness = 0.08;
inline constexpr double kDefaultMaxWallThickness = 0.5;
inline constexpr double kDefaultDetectionSensitivity = 0.5;
inline constexpr double kDefaultMinWallLength = 0.3;
inline constexpr double kDefaultMaxOpeningWidth = 2.5;

// Settings that steer detection.
struct DetectionParams {
    // Dark strokes thinner than this are not walls: text, dimension lines, door and window symbols.
    double minThickness = kDefaultMinWallThickness;
    // Dark areas thicker than this are not walls: filled blocks, legends, solid columns.
    double maxThickness = kDefaultMaxWallThickness;
    // From 0 to 1. Higher counts lighter grey as ink. For the built-in detector the ink level is Otsu's
    // threshold of the image times (0.5 + sensitivity), so 0.5 uses Otsu's threshold as it is.
    double sensitivity = kDefaultDetectionSensitivity;
    // Wall pieces shorter than this are dropped.
    double minLength = kDefaultMinWallLength;
    // A gap up to this wide between two wall pieces in line with the same thickness is closed with wall,
    // and the gap is proposed as an opening. 0 closes no gaps.
    double maxOpeningWidth = kDefaultMaxOpeningWidth;
    bool rooms = true;    // propose rooms from the closed areas of the proposed walls
    bool openings = true; // propose doors, windows and plain openings in the closed gaps

    bool operator==(const DetectionParams&) const = default;
};

// Checks the settings. InvalidValue when a number is not finite, minThickness, maxThickness or minLength is
// not above zero, maxOpeningWidth is below zero, minThickness is not below maxThickness, or sensitivity is
// outside 0 to 1.
Status checkDetectionParams(const DetectionParams& params);

// How a proposal relates to what the floor already has (see markExisting).
enum class ProposalState {
    New,      // Accept adds it
    Existing, // the floor already has it; Accept skips it and the preview hides it
    Overlaps, // part of it runs along an existing wall; Accept skips it and the preview hides it
};

// A proposed straight wall between two proposed points.
struct ProposedWall {
    std::size_t from = 0; // index into DetectionResult::points
    std::size_t to = 0;
    double thickness = kDefaultWallThickness;
    ProposalState state = ProposalState::New;

    bool operator==(const ProposedWall&) const = default;
};

// A proposed door, window or plain opening on a proposed wall.
// For a door, hinge is the jamb the swing arc starts from and swing the side of the wall the arc lies on,
// both relative to the proposed wall's from and to points (see HingeEnd and WallSide in Plan.h). Other types
// keep Start and Right.
struct ProposedOpening {
    std::size_t wall = 0;   // index into DetectionResult::walls
    OpeningType type = OpeningType::Plain;
    double offset = 0.0;    // along the wall from its from point to the centre of the opening
    double width = 0.0;
    HingeEnd hinge = HingeEnd::Start;
    WallSide swing = WallSide::Right;
    ProposalState state = ProposalState::New; // Existing when the floor already has an opening there

    bool operator==(const ProposedOpening&) const = default;
};

// A proposed room: one closed area of the proposed walls, as findClosedAreas gives it.
struct ProposedRoom {
    std::vector<std::size_t> walls;       // indexes into DetectionResult::walls, outer ring then islands
    std::vector<std::vector<Vec2>> rings; // centre line corners of each ring, outer first
    double centreLineArea = 0.0;
    Vec2 inside;                          // a position well inside, where Accept adds the room
    ProposalState state = ProposalState::New; // Existing when a room of the floor already holds inside

    bool operator==(const ProposedRoom&) const = default;
};

// Everything one detection run found. A valid result (see checkDetectionResult) has points at least 1 mm
// apart, walls joining two different points with thickness above zero, at most one wall between any two
// points, and walls that meet only at shared points: a T junction is a point shared by three walls.
struct DetectionResult {
    Status status;
    bool cancelled = false;    // stopped early; the lists are then empty
    std::string detector;      // id of the detector that made it
    double pixelsPerMetre = 0.0;
    DetectionParams params;
    int imageWidth = 0;        // pixels
    int imageHeight = 0;
    std::vector<Vec2> points;
    std::vector<ProposedWall> walls;
    std::vector<ProposedOpening> openings;
    std::vector<ProposedRoom> rooms;
    double elapsedMs = 0.0;    // time spent detecting, image loading not included
};

// Checks the rules of a valid result listed above, plus indexes in range and finite numbers.
// InvalidValue naming the first broken rule.
Status checkDetectionResult(const DetectionResult& result);

// Replaces result.rooms with the closed areas of the proposed walls, found with findClosedAreas on a floor
// made of the proposals, in its stable order. States are New until markExisting runs.
void proposeRooms(DetectionResult& result);

// A copy of a detected result in plan coordinates: every point, room ring and room inside position moved by the
// floor's image offset, as imageToPlan does. Lengths, areas, states and everything else stay. The only place
// this shift is worked out. Give it a detected result, never one it has already moved.
DetectionResult proposalsOnPlan(const Floor& floor, DetectionResult detected);

// Sets the state of every proposed wall, opening and room against what the floor has now. The result is in
// plan coordinates (proposalsOnPlan).
// A wall is checked at places every 0.05 m along it, leaving out one tolerance at each end, or at its middle
// when it is shorter. A place is covered when it lies within the tolerance of an existing straight wall
// running the same way (within 5 degrees). The tolerance is half the larger of the two thicknesses, and at
// least 0.05 m. Every place covered: Existing. Some covered: Overlaps. None: New.
// An opening is Existing when an opening of the floor covers its centre: the centre is within the tolerance
// of that opening's wall centre line and between its two jambs. Otherwise New. Overlaps is not used.
// A room is Existing when roomAt finds a room at its inside position, otherwise New.
void markExisting(const Floor& floor, DetectionResult& result);

// How many proposals of each kind there are, for the app panel and the CLI answer.
struct ProposalCounts {
    std::size_t points = 0;
    std::size_t walls = 0;
    std::size_t newWalls = 0;
    std::size_t existingWalls = 0;
    std::size_t overlappingWalls = 0;
    std::size_t rooms = 0;
    std::size_t newRooms = 0;
    std::size_t openings = 0;
    std::size_t newOpenings = 0;
    std::size_t existingOpenings = 0;

    bool operator==(const ProposalCounts&) const = default;
};

// Counts the proposals by kind and state.
ProposalCounts countProposals(const DetectionResult& result);

// What detection needs from a floor: the image file on disk and the scale.
struct DetectionSource {
    Status status;
    std::filesystem::path imageFile;
    double pixelsPerMetre = 0.0;
};

// Finds the image and scale of a floor, so the app and the CLI refuse in the same words. NotFound for an
// unknown floor or a floor without an image. InvalidValue when the plan has no scale yet. projectFolder may
// be empty for a project that was never saved; its images are then absolute.
DetectionSource detectionSource(const Plan& plan, const std::filesystem::path& projectFolder,
                                std::string_view floorId);

}
