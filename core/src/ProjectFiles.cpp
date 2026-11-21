#include "plynth/core/ProjectFiles.h"

#include "FileIo.h"

#include "plynth/core/ProjectJson.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// Reads a whole file into text.
Status readText(const fs::path& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail(ErrorCode::CannotRead, "The file " + pathToUtf8(path) + " could not be opened.");
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) return fail(ErrorCode::CannotRead, "The file " + pathToUtf8(path) + " could not be read.");
    out = buffer.str();
    return {};
}

// The text of a JSON file: 2 space indent and a final line break.
std::string dumpJson(const Json& json)
{
    return json.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

// Reads and parses one JSON file. The message of an error starts with the file name.
Status readJsonFile(const fs::path& path, const std::string& name, Json& out)
{
    std::string text;
    if (Status s = readText(path, text); !s.ok()) return s;
    out = Json::parse(text, nullptr, false);
    if (out.is_discarded()) return fail(ErrorCode::BadFile, name + " is not valid JSON.");
    return {};
}

// Puts the file name in front of an error from a reader.
Status withFileName(Status s, const std::string& name)
{
    if (!s.ok()) s.message = name + ": " + s.message;
    return s;
}

// Reads a project from a folder that holds project.json and plan.json. missingCode and missingText say
// what to report when project.json is not there.
ProjectLoad loadFrom(const fs::path& folder, ErrorCode missingCode, const std::string& missingText)
{
    ProjectLoad result;
    const std::string projectName(kProjectFileName);
    const std::string planName(kPlanFileName);
    std::error_code ec;
    const fs::path projectPath = folder / pathFromUtf8(kProjectFileName);
    const fs::path planPath = folder / pathFromUtf8(kPlanFileName);
    if (!fs::is_regular_file(projectPath, ec)) {
        result.status = fail(missingCode, missingText);
        return result;
    }
    if (!fs::is_regular_file(planPath, ec)) {
        result.status = fail(ErrorCode::NotFound, planName + " is missing from " + pathToUtf8(folder) + ".");
        return result;
    }

    Json projectJson;
    if (Status s = readJsonFile(projectPath, projectName, projectJson); !s.ok()) {
        result.status = s;
        return result;
    }
    SettingsRead settings = settingsFromJson(projectJson);
    if (!settings.status.ok()) {
        result.status = withFileName(settings.status, projectName);
        return result;
    }

    Json planJson;
    if (Status s = readJsonFile(planPath, planName, planJson); !s.ok()) {
        result.status = s;
        return result;
    }
    PlanRead plan = planFromJson(planJson);
    if (!plan.status.ok()) {
        result.status = withFileName(plan.status, planName);
        return result;
    }

    result.project.settings = std::move(settings.settings);
    result.project.plan = std::move(plan.plan);
    result.migrated = settings.migrated || plan.migrated;
    return result;
}

// Checks the settings that checkPlan does not cover.
Status checkSettings(const ProjectSettings& settings)
{
    if (settings.name.empty()) return fail(ErrorCode::InvalidValue, "The project name is empty.");
    if (!std::isfinite(settings.gridSize) || settings.gridSize <= 0.0) {
        return fail(ErrorCode::InvalidValue, "The grid size must be a number above zero.");
    }
    return {};
}

// Writes plan.json and project.json into a folder. Both temporary files are written first, so a full disk
// fails before anything is replaced. If the second rename then fails, the first file is put back, so the
// folder is left as it was whenever an error is reported.
Status writeJsonFiles(const Project& project, const fs::path& folder)
{
    const fs::path planPath = folder / pathFromUtf8(kPlanFileName);
    const fs::path projectPath = folder / pathFromUtf8(kProjectFileName);
    fs::path planTmp;
    fs::path projectTmp;
    if (Status s = fileio::writeTemp(planPath, dumpJson(planToJson(project.plan)), planTmp); !s.ok()) return s;
    if (Status s = fileio::writeTemp(projectPath, dumpJson(settingsToJson(project.settings)), projectTmp); !s.ok()) {
        std::error_code ignore;
        fs::remove(planTmp, ignore);
        return s;
    }

    std::string oldPlan;
    const bool hadPlan = readText(planPath, oldPlan).ok();
    if (Status s = fileio::renameOver(planTmp, planPath); !s.ok()) {
        std::error_code ignore;
        fs::remove(projectTmp, ignore);
        return s;
    }
    if (Status s = fileio::renameOver(projectTmp, projectPath); !s.ok()) {
        if (hadPlan) {
            fileio::writeAtomic(planPath, oldPlan);
        } else {
            std::error_code ignore;
            fs::remove(planPath, ignore);
        }
        return s;
    }
    return {};
}

// Finds a file name that is free in a folder, adding -2, -3 and so on before the extension.
fs::path freeName(const fs::path& folder, const fs::path& fileName)
{
    std::error_code ec;
    if (!fs::exists(folder / fileName, ec)) return fileName;
    for (int n = 2;; ++n) {
        fs::path stem = fileName.stem();
        stem += "-" + std::to_string(n);
        stem += fileName.extension();
        if (!fs::exists(folder / stem, ec)) return stem;
    }
}

}

// Turns UTF-8 text into a path.
fs::path pathFromUtf8(std::string_view text)
{
    const std::u8string u8(reinterpret_cast<const char8_t*>(text.data()), text.size());
    return fs::path(u8);
}

// Turns a path into UTF-8 text with forward slashes.
std::string pathToUtf8(const fs::path& path)
{
    const std::u8string u8 = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// Where a floor image lives on disk.
fs::path resolveImagePath(const fs::path& projectFolder, std::string_view image)
{
    const fs::path p = pathFromUtf8(image);
    if (p.is_absolute() || projectFolder.empty()) return p;
    return projectFolder / p;
}

// Says whether a folder holds a project.
bool isProjectFolder(const fs::path& folder)
{
    std::error_code ec;
    return fs::is_regular_file(folder / pathFromUtf8(kProjectFileName), ec);
}

// Reads project.json and plan.json from a project folder.
ProjectLoad loadProject(const fs::path& folder)
{
    return loadFrom(folder, ErrorCode::NotAProject,
                    "The folder " + pathToUtf8(folder) + " is not a Plynth project: " + std::string(kProjectFileName) + " is missing.");
}

// Copies images into plans/, then writes both JSON files.
Status saveProject(Project& project, const fs::path& folder, const fs::path& sourceFolder)
{
    if (Status s = checkPlan(project.plan); !s.ok()) return s;
    if (Status s = checkSettings(project.settings); !s.ok()) return s;

    struct Job {
        Floor* floor = nullptr;
        fs::path source;
    };
    std::vector<Job> jobs;
    std::error_code ec;
    for (Floor& floor : project.plan.floors) {
        if (!floor.image) continue;
        const fs::path source = resolveImagePath(sourceFolder, *floor.image);
        if (!fs::is_regular_file(source, ec)) {
            return fail(ErrorCode::NotFound, "The plan image " + pathToUtf8(source) + " could not be found.");
        }
        jobs.push_back({&floor, source});
    }

    const fs::path plansDir = folder / pathFromUtf8(kPlansFolderName);
    fs::create_directories(plansDir, ec);
    if (ec || !fs::is_directory(plansDir, ec)) {
        return fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(plansDir) + " could not be created.");
    }

    Project result = project;
    std::map<fs::path, std::string> copied;
    std::vector<fs::path> created;
    const auto removeCreated = [&created]() {
        std::error_code ignore;
        for (const fs::path& file : created) fs::remove(file, ignore);
    };
    for (const Job& job : jobs) {
        std::string relative;
        if (fs::equivalent(job.source.parent_path(), plansDir, ec)) {
            relative = std::string(kPlansFolderName) + "/" + pathToUtf8(job.source.filename());
        } else {
            const fs::path key = fs::weakly_canonical(job.source, ec);
            auto found = copied.find(key);
            if (found != copied.end()) {
                relative = found->second;
            } else {
                const fs::path name = freeName(plansDir, job.source.filename());
                fs::copy_file(job.source, plansDir / name, fs::copy_options::none, ec);
                if (ec) {
                    std::error_code ignore;
                    fs::remove(plansDir / name, ignore);
                    removeCreated();
                    return fail(ErrorCode::CannotWrite, "The plan image could not be copied to " + pathToUtf8(plansDir / name) + ".");
                }
                created.push_back(plansDir / name);
                relative = std::string(kPlansFolderName) + "/" + pathToUtf8(name);
                copied[key] = relative;
            }
        }
        Floor* target = findFloor(result.plan, job.floor->id);
        target->image = relative;
    }

    if (Status s = writeJsonFiles(result, folder); !s.ok()) {
        removeCreated();
        return s;
    }
    project = std::move(result);
    return {};
}

// Same as saveProject, but refuses when the folder already holds a project.
Status createProject(Project& project, const fs::path& folder, const fs::path& sourceFolder)
{
    if (isProjectFolder(folder)) {
        return fail(ErrorCode::AlreadyExists, "The folder " + pathToUtf8(folder) + " already holds a project.");
    }
    return saveProject(project, folder, sourceFolder);
}

// Looks for the autosave of a project folder.
AutosaveInfo findAutosave(const fs::path& folder)
{
    AutosaveInfo info;
    std::error_code ec;
    const fs::path dir = folder / pathFromUtf8(kAutosaveFolderName);
    const fs::path planPath = dir / pathFromUtf8(kPlanFileName);
    const fs::path projectPath = dir / pathFromUtf8(kProjectFileName);
    if (!fs::is_regular_file(planPath, ec) || !fs::is_regular_file(projectPath, ec)) return info;
    info.exists = true;
    const auto planTime = fs::last_write_time(planPath, ec);
    const auto projectTime = fs::last_write_time(projectPath, ec);
    info.time = std::max(planTime, projectTime);
    const fs::path savedPlan = folder / pathFromUtf8(kPlanFileName);
    if (fs::is_regular_file(savedPlan, ec)) {
        const auto savedTime = fs::last_write_time(savedPlan, ec);
        info.olderThanSaved = savedTime > info.time;
    }
    return info;
}

// Writes the project into the autosave folder.
Status writeAutosave(const Project& project, const fs::path& folder)
{
    if (Status s = checkPlan(project.plan); !s.ok()) return s;
    if (Status s = checkSettings(project.settings); !s.ok()) return s;
    std::error_code ec;
    const fs::path dir = folder / pathFromUtf8(kAutosaveFolderName);
    fs::create_directories(dir, ec);
    if (ec || !fs::is_directory(dir, ec)) {
        return fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(dir) + " could not be created.");
    }
    return writeJsonFiles(project, dir);
}

// Reads the autosave of a project folder.
ProjectLoad loadAutosave(const fs::path& folder)
{
    const fs::path dir = folder / pathFromUtf8(kAutosaveFolderName);
    return loadFrom(dir, ErrorCode::NotFound, "There is no autosave in " + pathToUtf8(folder) + ".");
}

// Deletes the autosave folder.
Status removeAutosave(const fs::path& folder)
{
    std::error_code ec;
    const fs::path dir = folder / pathFromUtf8(kAutosaveFolderName);
    fs::remove_all(dir, ec);
    if (ec) return fail(ErrorCode::CannotWrite, "The autosave in " + pathToUtf8(folder) + " could not be removed.");
    return {};
}

}
