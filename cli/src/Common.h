#pragma once

#include "Registry.h"

#include <plynth/core/Command.h>
#include <plynth/core/Document.h>
#include <plynth/core/ProjectFiles.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace plynth::cli {

// Adds the --project option, which names the project folder. Without it the current folder is used.
void addProjectOption(CLI::App& sub, std::string& value);

// The project folder to work on: the --project value, or the current folder. Absolute, forward slashes
// when printed.
std::filesystem::path resolveProjectFolder(const RunContext& context, const std::string& projectOption);

// Checks the edit lock (allowCliCommand) and then opens the project in a folder into a document.
// readOnly true is for commands that only read. On error the answer for command is returned in failed.
bool openProject(const std::string& command, const std::filesystem::path& folder, Document& document, Answer& failed,
                 bool readOnly = false);

// Checks the edit lock for a command that reads the project without a document. Returns false and
// fills failed when the command is refused.
bool checkLock(const std::string& command, const std::filesystem::path& folder, bool readOnly, Answer& failed);

// Saves the document's project into its own folder, leaving any autosave alone. Floor images that could
// not be found are listed in report when it is given.
Status saveInPlace(const Document& document, SaveReport* report = nullptr);

// Adds an image_missing warning to the answer for each plan image the save could not find.
void addSaveWarnings(Answer& answer, const SaveReport& report);

// Describes a project for project info and project new.
Json projectInfoJson(const std::filesystem::path& folder, const Project& project);

// The floor that commands use when the user gave none: the first one. Empty when there is none.
std::string defaultFloorId(const Plan& plan);

// Opens the project (edit lock checked), runs body on the document and saves when body's answer is ok.
// Nothing is saved when the answer is a failure.
Answer editProject(const std::string& command, const RunContext& context, const std::string& projectOption,
                   const std::function<Answer(Document&)>& body);

// Runs one command through the document. Returns false and fills failed when it is refused.
bool runCommand(const std::string& command, Document& document, std::unique_ptr<Command> toRun, Answer& failed);

// Loads the project, runs one command through the document and saves. Nothing is saved when the
// command is refused. After a success, buildResult makes the result from the changed document.
template <typename MakeCommand, typename BuildResult>
Answer edit(const std::string& command, const RunContext& context, const std::string& projectOption,
            MakeCommand makeCommand, BuildResult buildResult) {
    return editProject(command, context, projectOption, [&](Document& document) {
        auto made = makeCommand(document.plan());
        auto* raw = made.get();
        Answer failed;
        if (!runCommand(command, document, std::move(made), failed)) {
            return failed;
        }
        return success(command, buildResult(document.plan(), *raw));
    });
}

// The floor named by the --floor value, or the first floor when it is empty. Null, with failed filled,
// when there is none.
const Floor* resolveFloor(const std::string& command, const Plan& plan, const std::string& floorOption,
                          Answer& failed);

// The point entry shared by the point commands: id, floor, x, y, corner_radius, corner_radius_used and the walls
// that use it.
Json pointEntry(const Floor& floor, const Point& point);

// The wall entry shared by the wall commands: ends, thickness, height, bulge, length along the curve, radius,
// height used, the rooms that list it and the openings on it.
Json wallEntry(const Floor& floor, const Wall& wall);

// The floor entry shared by the floor commands: id, name, image, ceiling height, slab thickness, image offset,
// elevation, rise, the ids of its stairs and the number of roof sections above it.
Json floorEntry(const Plan& plan, const Floor& floor);

// The opening entry shared by the opening commands: wall, offset, type, shape, size, sill, hinge, swing, frame,
// auto_frame, centre on the plan, whether it fits its wall and the reason when it does not.
Json openingEntry(const Floor& floor, const Opening& opening);

// The room entry shared by the room commands: type, type name, walls, whether it is closed, area, label spot and
// whether set dressing adds props to it.
Json roomEntry(const Floor& floor, const Room& room);

// Removes one point or wall together with everything that depends on it, as one step (makeDeleteCommand).
// kind is "point" or "wall": an id of another kind is refused with invalid_value. The answer lists what was removed.
Answer removeCascade(const std::string& command, const std::string& kind, const RunContext& context,
                     const std::string& projectOption,
                     const Id& id);

// For the remove commands that do not cascade: when id names a stair, the refusal that points to stair remove.
// Empty when the id is not a stair or the project cannot be read, so the normal path reports it.
std::optional<Answer> refuseStairId(const std::string& command, const std::string& kind, const RunContext& context,
                                    const std::string& projectOption, const Id& id);

// The result of a remove command: the ids of the items, detached items, openings, rooms, walls and points that
// were removed. A detached item stays on the plan but is no longer on its removed wall.
Json removedJson(const std::vector<Id>& items, const std::vector<Id>& detached, const std::vector<Id>& openings,
                 const std::vector<Id>& rooms, const std::vector<Id>& walls, const std::vector<Id>& points);

}
