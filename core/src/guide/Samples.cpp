#include "plynth/core/Samples.h"

#include "plynth/core/ProjectFiles.h"
#include "plynth/core/Resources.h"
#include "plynth/core/catalog/AssetInfo.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

using Json = nlohmann::ordered_json;

namespace {

// Thrown while reading the index, with a JSON pointer to the bad value.
struct IndexProblem {
    std::string where;
    std::string message;
};

// The text at key, which must be a string. Empty text is refused when needed is true.
std::string textAt(const Json& object, const char* key, const std::string& pointer, bool needed = true)
{
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        throw IndexProblem{pointer + "/" + key, std::string("The value \"") + key + "\" must be text."};
    }
    std::string text = it->get<std::string>();
    if (needed && text.empty()) {
        throw IndexProblem{pointer + "/" + key, std::string("The value \"") + key + "\" may not be empty."};
    }
    return text;
}

// The array at key. A missing key gives an empty array when optional is true.
const Json& arrayAt(const Json& object, const char* key, const std::string& pointer, bool optional = false)
{
    static const Json empty = Json::array();
    const auto it = object.find(key);
    if (it == object.end() && optional) return empty;
    if (it == object.end() || !it->is_array()) {
        throw IndexProblem{pointer + "/" + key, std::string("The value \"") + key + "\" must be a list."};
    }
    return *it;
}

// A list of text values.
std::vector<std::string> textListAt(const Json& object, const char* key, const std::string& pointer)
{
    std::vector<std::string> out;
    const Json& list = arrayAt(object, key, pointer, true);
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (!list[i].is_string()) {
            throw IndexProblem{pointer + "/" + key + "/" + std::to_string(i), "Each value must be text."};
        }
        out.push_back(list[i].get<std::string>());
    }
    return out;
}

// An id that follows the catalog id rules.
std::string idAt(const Json& object, const std::string& pointer)
{
    std::string id = textAt(object, "id", pointer);
    if (Status s = checkCatalogId(id); !s.ok()) throw IndexProblem{pointer + "/id", s.message};
    return id;
}

// A relative path that stays inside the samples folder, written with "/".
std::string relativePathAt(const Json& object, const char* key, const std::string& pointer)
{
    std::string text = textAt(object, key, pointer);
    const std::string where = pointer + "/" + key;
    if (text.find(':') != std::string::npos || text.front() == '/' || text.front() == '\\') {
        throw IndexProblem{where, "The path \"" + text + "\" must be relative to the samples folder."};
    }
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find_first_of("/\\", start);
        if (end == std::string::npos) end = text.size();
        const std::string part = text.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") {
            throw IndexProblem{where, "The path \"" + text + "\" must stay inside the samples folder."};
        }
        if (end == text.size()) break;
        start = end + 1;
    }
    return text;
}

// Refuses an id used twice in one list.
void checkUnique(std::set<std::string>& seen, const std::string& id, const std::string& pointer)
{
    if (!seen.insert(id).second) throw IndexProblem{pointer + "/id", "The id \"" + id + "\" is listed twice."};
}

// Reads every value of the index.
SamplesIndex readIndex(const Json& json)
{
    if (!json.is_object()) throw IndexProblem{"", "The samples index must be a JSON object."};
    if (textAt(json, "format", "") != kSamplesFormat) {
        throw IndexProblem{"/format", "The file is not a Plynth samples index."};
    }
    SamplesIndex index;
    index.license = textAt(json, "license", "");
    if (Status s = checkCatalogId(index.license); !s.ok()) throw IndexProblem{"/license", s.message};

    const Json& assets = arrayAt(json, "assets", "");
    std::set<std::string> seen;
    for (std::size_t i = 0; i < assets.size(); ++i) {
        const std::string p = "/assets/" + std::to_string(i);
        if (!assets[i].is_object()) throw IndexProblem{p, "Each asset must be a JSON object."};
        SampleAsset a;
        a.id = idAt(assets[i], p);
        checkUnique(seen, a.id, p);
        a.name = textAt(assets[i], "name", p);
        a.file = relativePathAt(assets[i], "file", p);
        a.categories = textListAt(assets[i], "categories", p);
        a.tags = textListAt(assets[i], "tags", p);
        index.assets.push_back(std::move(a));
    }

    const Json& materials = arrayAt(json, "materials", "");
    seen.clear();
    for (std::size_t i = 0; i < materials.size(); ++i) {
        const std::string p = "/materials/" + std::to_string(i);
        if (!materials[i].is_object()) throw IndexProblem{p, "Each material must be a JSON object."};
        SampleMaterial m;
        m.id = idAt(materials[i], p);
        checkUnique(seen, m.id, p);
        m.folder = relativePathAt(materials[i], "folder", p);
        index.materials.push_back(std::move(m));
    }

    const Json& projects = arrayAt(json, "projects", "", true);
    seen.clear();
    for (std::size_t i = 0; i < projects.size(); ++i) {
        const std::string p = "/projects/" + std::to_string(i);
        if (!projects[i].is_object()) throw IndexProblem{p, "Each project must be a JSON object."};
        SampleProject s;
        s.id = idAt(projects[i], p);
        checkUnique(seen, s.id, p);
        s.name = textAt(projects[i], "name", p);
        s.description = textAt(projects[i], "description", p, false);
        s.folder = relativePathAt(projects[i], "folder", p);
        s.image = relativePathAt(projects[i], "image", p);
        index.projects.push_back(std::move(s));
    }
    return index;
}

// A project name made safe as a folder name.
std::string folderNameFor(const std::string& name)
{
    std::string out;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos) continue;
        out.push_back(c);
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out.empty() ? std::string("Sample") : out;
}

// Says whether path is folder or inside it, after resolving both as far as they exist.
bool isInside(const fs::path& folder, const fs::path& path)
{
    std::error_code ec;
    const fs::path f = fs::weakly_canonical(folder, ec).lexically_normal();
    if (ec) return false;
    const fs::path p = fs::weakly_canonical(path, ec).lexically_normal();
    if (ec) return false;
    const fs::path rel = p.lexically_relative(f);
    return !rel.empty() && (rel == "." || *rel.begin() != "..");
}

// Copies the files of a sample project, and the master instructions file as AGENTS.md when one is given. False on
// the first failure.
bool copyProjectFiles(const fs::path& from, const fs::path& to, const fs::path& agentGuide)
{
    std::error_code ec;
    if (!agentGuide.empty() && fs::is_regular_file(agentGuide, ec) && !fs::exists(to / "AGENTS.md", ec)) {
        if (!fs::copy_file(agentGuide, to / "AGENTS.md", fs::copy_options::none, ec) || ec) return false;
    }
    for (const char* name : {"project.json", "plan.json", "export-options.json"}) {
        const fs::path source = from / name;
        if (!fs::is_regular_file(source, ec)) continue;
        if (!fs::copy_file(source, to / name, fs::copy_options::none, ec) || ec) return false;
    }
    const fs::path plans = from / "plans";
    if (fs::is_directory(plans, ec)) {
        fs::copy(plans, to / "plans", fs::copy_options::recursive, ec);
        if (ec) return false;
    }
    return true;
}

}

// Parses the text of samples.json.
SamplesRead parseSamples(std::string_view jsonText)
{
    SamplesRead out;
    const Json json = Json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
    if (json.is_discarded()) {
        out.status = fail(ErrorCode::BadFile, "The samples index is not valid JSON.");
        return out;
    }
    try {
        if (json.is_object()) {
            const auto version = json.find("version");
            if (version == json.end() || !version->is_number_integer()) {
                throw IndexProblem{"/version", "The value \"version\" must be a whole number."};
            }
            const auto number = version->get<long long>();
            if (number > kSamplesVersion) {
                out.status = fail(ErrorCode::UnsupportedVersion,
                                  "The samples index was written by a newer Plynth.", "/version");
                return out;
            }
            if (number < 1) throw IndexProblem{"/version", "The version must be 1 or more."};
        }
        out.index = readIndex(json);
    } catch (const IndexProblem& problem) {
        out.status = fail(ErrorCode::BadFile, problem.message, problem.where);
        out.index = {};
    }
    return out;
}

// Reads samples.json from a samples folder.
SamplesRead readSamples(const fs::path& samplesFolder)
{
    const fs::path file = samplesFolder / pathFromUtf8(kSamplesIndexFileName);
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        SamplesRead out;
        out.status = fail(ErrorCode::CannotRead, "The file " + pathToUtf8(file) + " could not be opened.");
        return out;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parseSamples(buffer.str());
}

// Copies one sample project into a free folder, removing it again when anything fails.
SampleCopy copySampleProject(const fs::path& samplesFolder, std::string_view id, const fs::path& into,
                             const fs::path& agentGuide)
{
    SampleCopy out;
    const SamplesRead read = readSamples(samplesFolder);
    if (!read.status.ok()) {
        out.status = read.status;
        return out;
    }
    const SampleProject* project = nullptr;
    for (const SampleProject& p : read.index.projects) {
        if (p.id == id) project = &p;
    }
    if (!project) {
        out.status = fail(ErrorCode::NotFound, "There is no sample project \"" + std::string(id) + "\".");
        return out;
    }
    std::error_code ec;
    const fs::path source = samplesFolder / pathFromUtf8(project->folder);
    if (!fs::is_regular_file(source / "project.json", ec)) {
        out.status = fail(ErrorCode::NotFound, "The sample project \"" + project->id + "\" has no project.json.");
        return out;
    }
    if (into.empty() || isInside(samplesFolder, into)) {
        out.status = fail(ErrorCode::InvalidValue, "A sample project cannot be opened inside the samples folder.");
        return out;
    }
    fs::create_directories(into, ec);
    if (!fs::is_directory(into, ec)) {
        out.status = fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(into) + " could not be created.");
        return out;
    }

    const std::string base = folderNameFor(project->name);
    for (int number = 1; number < 1000; ++number) {
        const std::string name = number == 1 ? base : base + " (" + std::to_string(number) + ")";
        const fs::path target = into / pathFromUtf8(name);
        if (fs::exists(target, ec)) continue;
        if (!fs::create_directory(target, ec) || ec) continue;
        if (!copyProjectFiles(source, target, agentGuide)) {
            fs::remove_all(target, ec);
            out.status = fail(ErrorCode::CannotWrite, "The sample project could not be copied to " + pathToUtf8(target) + ".");
            return out;
        }
        out.projectFolder = target;
        return out;
    }
    out.status = fail(ErrorCode::CannotWrite, "No free folder name was found in " + pathToUtf8(into) + ".");
    return out;
}

// The master instructions file for projects, or empty.
fs::path agentGuideMaster()
{
    const fs::path docs = resourceFolder(ResourceKind::Docs);
    if (docs.empty()) return {};
    std::error_code ec;
    const fs::path master = docs / "templates" / "project-agents.md";
    return fs::is_regular_file(master, ec) ? master : fs::path{};
}

}
