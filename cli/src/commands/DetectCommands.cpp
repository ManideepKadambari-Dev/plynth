#include "../Common.h"

#include <plynth/core/commands/DetectionCommands.h>
#include <plynth/core/detection/Detection.h>
#include <plynth/core/detection/Detector.h>
#include <plynth/core/detection/GrayImage.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace plynth::cli {

namespace {

struct DetectOptions {
    std::string project;
    std::string floor;
    std::string detector{kDefaultDetectorId};
    DetectionParams params;
    bool noRooms = false;
    bool noOpenings = false;
    bool apply = false;
};

// Rounds to a number of decimals, which hides the small error of the arithmetic in the answer.
double rounded(double value, double scale) {
    return std::round(value * scale) / scale;
}

// The ids of every registered detector, in order.
std::vector<std::string> detectorIds() {
    std::vector<std::string> ids;
    for (const Detector* detector : allDetectors()) {
        ids.emplace_back(detector->id());
    }
    return ids;
}

// The words for a proposal state.
const char* stateName(ProposalState state) {
    switch (state) {
    case ProposalState::New:
        return "new";
    case ProposalState::Existing:
        return "existing";
    case ProposalState::Overlaps:
        return "overlaps";
    }
    return "new";
}

// The params as the answer shows them.
Json paramsJson(const DetectionParams& params) {
    Json json = Json::object();
    json["min_thickness"] = params.minThickness;
    json["max_thickness"] = params.maxThickness;
    json["sensitivity"] = params.sensitivity;
    json["min_length"] = params.minLength;
    json["max_opening_width"] = params.maxOpeningWidth;
    json["rooms"] = params.rooms;
    json["openings"] = params.openings;
    return json;
}

// The counts as the answer shows them.
Json countsJson(const ProposalCounts& counts) {
    Json json = Json::object();
    json["points"] = counts.points;
    json["walls"] = counts.walls;
    json["new_walls"] = counts.newWalls;
    json["existing_walls"] = counts.existingWalls;
    json["overlapping_walls"] = counts.overlappingWalls;
    json["rooms"] = counts.rooms;
    json["new_rooms"] = counts.newRooms;
    json["openings"] = counts.openings;
    json["new_openings"] = counts.newOpenings;
    json["existing_openings"] = counts.existingOpenings;
    return json;
}

// Moves the proposed points and rooms from image coordinates to plan positions through the floor's image offset.
void shiftToPlan(const Floor& floor, double pixelsPerMetre, DetectionResult& result) {
    if (floor.imageOffset == ImageOffset{}) {
        return;
    }
    const auto move = [&](Vec2& p) {
        p = imageToPlan(floor, pixelsPerMetre, {p.x * pixelsPerMetre, p.y * pixelsPerMetre});
    };
    for (Vec2& p : result.points) {
        move(p);
    }
    for (ProposedRoom& room : result.rooms) {
        move(room.inside);
        for (std::vector<Vec2>& ring : room.rings) {
            for (Vec2& p : ring) {
                move(p);
            }
        }
    }
}

// The proposal lists of a result: points, walls, openings and rooms.
void addProposalLists(Json& json, const DetectionResult& result) {
    Json points = Json::array();
    for (std::size_t i = 0; i < result.points.size(); ++i) {
        Json item = Json::object();
        item["index"] = i;
        item["x"] = rounded(result.points[i].x, 1e4);
        item["y"] = rounded(result.points[i].y, 1e4);
        points.push_back(std::move(item));
    }
    Json walls = Json::array();
    for (std::size_t i = 0; i < result.walls.size(); ++i) {
        const ProposedWall& wall = result.walls[i];
        const Vec2 a = result.points[wall.from];
        const Vec2 b = result.points[wall.to];
        Json item = Json::object();
        item["index"] = i;
        item["from"] = wall.from;
        item["to"] = wall.to;
        item["thickness"] = wall.thickness;
        item["length"] = rounded(std::hypot(b.x - a.x, b.y - a.y), 1e2);
        item["state"] = stateName(wall.state);
        walls.push_back(std::move(item));
    }
    Json openings = Json::array();
    for (std::size_t i = 0; i < result.openings.size(); ++i) {
        const ProposedOpening& opening = result.openings[i];
        const Vec2 a = result.points[result.walls[opening.wall].from];
        const Vec2 b = result.points[result.walls[opening.wall].to];
        const double length = std::hypot(b.x - a.x, b.y - a.y);
        const double along = length > 0.0 ? opening.offset / length : 0.0;
        Json item = Json::object();
        item["index"] = i;
        item["wall"] = opening.wall;
        item["type"] = std::string(openingTypeName(opening.type));
        item["offset"] = rounded(opening.offset, 1e2);
        item["width"] = rounded(opening.width, 1e2);
        item["hinge"] = std::string(hingeEndName(opening.hinge));
        item["swing"] = std::string(wallSideName(opening.swing));
        item["state"] = stateName(opening.state);
        item["x"] = rounded(a.x + (b.x - a.x) * along, 1e2);
        item["y"] = rounded(a.y + (b.y - a.y) * along, 1e2);
        openings.push_back(std::move(item));
    }
    Json rooms = Json::array();
    for (std::size_t i = 0; i < result.rooms.size(); ++i) {
        const ProposedRoom& room = result.rooms[i];
        Json inside = Json::object();
        inside["x"] = rounded(room.inside.x, 1e4);
        inside["y"] = rounded(room.inside.y, 1e4);
        Json item = Json::object();
        item["index"] = i;
        item["walls"] = room.walls;
        item["inside"] = std::move(inside);
        item["centre_line_area"] = rounded(room.centreLineArea, 1e4);
        item["state"] = stateName(room.state);
        rooms.push_back(std::move(item));
    }
    json["points"] = std::move(points);
    json["walls"] = std::move(walls);
    json["openings"] = std::move(openings);
    json["rooms"] = std::move(rooms);
}

// What an apply did, as the answer shows it.
Json appliedJson(const ApplyDetectionCommand* command) {
    Json applied = Json::object();
    applied["walls"] = command ? command->wallIds() : std::vector<Id>();
    applied["points"] = command ? command->pointIds() : std::vector<Id>();
    applied["split_walls"] = command ? command->splitWallIds() : std::vector<Id>();
    applied["openings"] = command ? command->openingIds() : std::vector<Id>();
    applied["rooms"] = command ? command->roomIds() : std::vector<Id>();
    applied["skipped_walls"] = command ? command->skippedWalls() : std::size_t(0);
    applied["skipped_openings"] = command ? command->skippedOpenings() : std::size_t(0);
    applied["skipped_rooms"] = command ? command->skippedRooms() : std::size_t(0);
    return applied;
}

// Runs detection on a floor and lists the proposals. With apply, accepts the new ones in one step and saves.
Answer detect(const RunContext& context, const DetectOptions& given) {
    const std::string name = "detect";
    DetectOptions options = given;
    options.params.rooms = !options.noRooms;
    options.params.openings = !options.noOpenings;

    if (findDetector(options.detector) == nullptr) {
        std::string known;
        for (const std::string& id : detectorIds()) {
            known += (known.empty() ? "" : ", ") + id;
        }
        return failure(name, fail(ErrorCode::InvalidValue, "There is no detector " + options.detector +
                                                               ". The detectors are: " + known + "."));
    }
    const Status checked = checkDetectionParams(options.params);
    if (!checked.ok()) {
        return failure(name, checked);
    }

    const std::filesystem::path folder = resolveProjectFolder(context, options.project);
    Document document;
    Answer failed;
    if (!openProject(name, folder, document, failed, !options.apply)) {
        return failed;
    }
    const Floor* floor = resolveFloor(name, document.plan(), options.floor, failed);
    if (floor == nullptr) {
        return failed;
    }
    const DetectionSource source = detectionSource(document.plan(), document.folder(), floor->id);
    if (!source.status.ok()) {
        const bool noScale = source.status.code == ErrorCode::InvalidValue;
        return failure(name, noScale ? fail(ErrorCode::InvalidValue,
                                            "Set the scale first. Run 'scale from-points' or 'scale set'.")
                                     : source.status);
    }
    const GrayImageLoad loaded = loadGrayImage(source.imageFile);
    if (!loaded.status.ok()) {
        return failure(name, loaded.status);
    }
    DetectionResult result =
        runDetection(options.detector, *loaded.image, source.pixelsPerMetre, options.params);
    if (!result.status.ok()) {
        return failure(name, result.status);
    }
    // Proposals are measured from the image corner; the floor's image may sit at an offset, so the listed
    // positions and the comparison with existing walls use plan positions. Accepting takes the unshifted result.
    DetectionResult shown = result;
    shiftToPlan(*floor, source.pixelsPerMetre, shown);
    markExisting(*floor, shown);
    const ProposalCounts counts = countProposals(shown);

    Json json = Json::object();
    json["floor"] = floor->id;
    json["detector"] = result.detector;
    json["detectors"] = detectorIds();
    json["pixels_per_metre"] = source.pixelsPerMetre;
    Json image = Json::object();
    image["width"] = result.imageWidth;
    image["height"] = result.imageHeight;
    json["image"] = std::move(image);
    json["params"] = paramsJson(result.params);
    json["elapsed_ms"] = rounded(result.elapsedMs, 10.0);
    json["counts"] = countsJson(counts);
    addProposalLists(json, shown);

    if (!options.apply) {
        return success(name, std::move(json));
    }
    if (counts.newWalls == 0 && counts.newRooms == 0 && counts.newOpenings == 0) {
        json["applied"] = appliedJson(nullptr);
        return success(name, std::move(json));
    }
    auto command = std::make_unique<ApplyDetectionCommand>(floor->id, result, options.params.rooms);
    const ApplyDetectionCommand* raw = command.get();
    if (!runCommand(name, document, std::move(command), failed)) {
        return failed;
    }
    json["applied"] = appliedJson(raw);
    SaveReport report;
    const Status saved = saveInPlace(document, &report);
    if (!saved.ok()) {
        return failure(name, saved);
    }
    Answer applied = success(name, std::move(json));
    addSaveWarnings(applied, report);
    return applied;
}

}

std::vector<CommandSpec> detectCommands() {
    CommandSpec detectSpec;
    detectSpec.words = {"detect"};
    detectSpec.description = "Find walls, rooms and openings in the plan image. With --apply, add them to the plan.";
    detectSpec.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<DetectOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        sub.add_option("--detector", options->detector, "Detector id. Default: orthogonal.")->type_name("id");
        sub.add_option("--min-thickness", options->params.minThickness,
                       "Thinnest wall in metres. Default: 0.08.")
            ->type_name("metres");
        sub.add_option("--max-thickness", options->params.maxThickness,
                       "Thickest wall in metres. Default: 0.5.")
            ->type_name("metres");
        sub.add_option("--sensitivity", options->params.sensitivity,
                       "From 0 to 1. Higher counts lighter grey as ink. Default: 0.5.")
            ->type_name("0..1");
        sub.add_option("--min-length", options->params.minLength, "Shortest wall in metres. Default: 0.3.")
            ->type_name("metres");
        sub.add_option("--max-opening-width", options->params.maxOpeningWidth,
                       "Widest gap closed with wall and shown as an opening, in metres. 0 closes none. Default: 2.5.")
            ->type_name("metres");
        sub.add_flag("--no-rooms", options->noRooms, "Do not propose rooms, and add none with --apply.");
        sub.add_flag("--no-openings", options->noOpenings, "Do not propose openings, and add none with --apply.");
        sub.add_flag("--apply", options->apply, "Add the new walls, rooms and openings to the plan and save.");
        return [options](const RunContext& context) { return detect(context, *options); };
    };
    return {detectSpec};
}

}
