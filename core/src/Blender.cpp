#include "plynth/core/Blender.h"

#include "BlenderDiscovery.h"
#include "BlenderPaths.h"
#include "FileIo.h"

#include "plynth/core/BlenderJob.h"
#include "plynth/core/Process.h"
#include "plynth/core/ProjectFiles.h"

#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace plynth {

namespace fs = std::filesystem;

namespace blenderpaths {

namespace {

// Text in upper case, for comparing paths without case.
std::wstring caseKey(std::wstring text)
{
    if (text.empty()) return text;
    std::wstring upper(text.size(), L'\0');
    const int got = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, text.c_str(), static_cast<int>(text.size()),
                                  upper.data(), static_cast<int>(upper.size()), nullptr, nullptr, 0);
    if (got <= 0) return text;
    upper.resize(static_cast<std::size_t>(got));
    return upper;
}

// A name with the dots and spaces at its end cut off. A name of only dots and spaces stays as it is.
std::wstring trimmedName(std::wstring name)
{
    const std::size_t end = name.find_last_not_of(L". ");
    if (end != std::wstring::npos) name.erase(end + 1);
    return name;
}

// The same path with every name after the root trimmed.
fs::path trimmedNames(const fs::path& path)
{
    fs::path out = path.root_path();
    for (const fs::path& part : path.relative_path()) out /= trimmedName(part.wstring());
    return out;
}

// The texts a path is compared by: as given and resolved, each also with its names trimmed.
std::vector<std::wstring> comparedForms(const fs::path& path)
{
    std::vector<std::wstring> forms;
    for (const fs::path& form : {normalFolder(path), resolvedPath(path)}) {
        if (form.empty()) continue;
        forms.push_back(caseKey(form.wstring()));
        forms.push_back(caseKey(normalFolder(trimmedNames(form)).wstring()));
    }
    return forms;
}

// Says whether one text is the other or names something below it.
bool within(const std::wstring& inner, const std::wstring& outer)
{
    if (inner == outer) return true;
    const std::wstring prefix = !outer.empty() && outer.back() == L'\\' ? outer : outer + L'\\';
    return inner.size() > prefix.size() && inner.compare(0, prefix.size(), prefix) == 0;
}

}

// Reads one environment variable.
std::wstring environmentText(const wchar_t* name)
{
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    const DWORD got = GetEnvironmentVariableW(name, value.data(), size);
    value.resize(got);
    return value;
}

// Works out where Plynth's private Blender folder is. Factory startup alone does not protect the user's Blender
// folders, so every such run is pointed here.
fs::path privateFolder()
{
    if (const std::wstring set = environmentText(L"PLYNTH_BLENDER_PRIVATE"); !set.empty()) return fs::path(set);
    if (const std::wstring local = environmentText(L"LOCALAPPDATA"); !local.empty()) return fs::path(local) / L"Plynth" / L"blender-private";
    std::error_code ec;
    return fs::temp_directory_path(ec) / L"Plynth" / L"blender-private";
}

// Tidies the text of a path without looking at the disk.
fs::path normalFolder(const fs::path& path)
{
    if (path.empty()) return {};
    std::wstring text = path.lexically_normal().wstring();
    std::replace(text.begin(), text.end(), L'/', L'\\');
    const std::size_t keep = fs::path(text).root_path().wstring().size();
    while (text.size() > keep && text.back() == L'\\') text.pop_back();
    return fs::path(text);
}

// Compares two paths by their tidied text.
bool sameText(const fs::path& a, const fs::path& b)
{
    return caseKey(normalFolder(a).wstring()) == caseKey(normalFolder(b).wstring());
}

// Asks Windows where a path really leads, for the longest part of it that exists.
fs::path resolvedPath(const fs::path& path)
{
    if (path.empty() || !path.is_absolute()) return {};
    fs::path existing = normalFolder(path);
    std::vector<fs::path> rest;
    for (;;) {
        // Attributes only, so this never counts as reading a file and never meets a file held open by another.
        const HANDLE handle = CreateFileW(existing.c_str(), FILE_READ_ATTRIBUTES,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                          FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            std::wstring final(MAX_PATH, L'\0');
            DWORD got = GetFinalPathNameByHandleW(handle, final.data(), static_cast<DWORD>(final.size()),
                                                  FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            if (got >= final.size()) {
                final.resize(got);
                got = GetFinalPathNameByHandleW(handle, final.data(), static_cast<DWORD>(final.size()),
                                                FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            }
            CloseHandle(handle);
            if (got == 0 || got >= final.size()) return {};
            final.resize(got);
            if (final.rfind(L"\\\\?\\UNC\\", 0) == 0) final = L"\\\\" + final.substr(8);
            else if (final.rfind(L"\\\\?\\", 0) == 0) final.erase(0, 4);
            fs::path out(final);
            for (auto it = rest.rbegin(); it != rest.rend(); ++it) out /= *it;
            return normalFolder(out);
        }
        if (!existing.has_relative_path()) return {};
        rest.push_back(existing.filename());
        existing = existing.parent_path();
    }
}

// Says whether two paths share a folder, one in the other or the same.
bool overlaps(const fs::path& a, const fs::path& b)
{
    if (a.empty() || b.empty()) return false;
    const std::vector<std::wstring> first = comparedForms(a);
    const std::vector<std::wstring> second = comparedForms(b);
    for (const std::wstring& x : first) {
        for (const std::wstring& y : second) {
            if (within(x, y) || within(y, x)) return true;
        }
    }
    return false;
}

// Looks for the "Blender Foundation" name in a path.
bool namesBlenderFoundation(const fs::path& path)
{
    const std::wstring wanted = caseKey(L"Blender Foundation");
    const auto named = [&](const fs::path& form) {
        for (const fs::path& part : normalFolder(form).relative_path()) {
            if (caseKey(trimmedName(part.wstring())) == wanted) return true;
        }
        return false;
    };
    return named(path) || named(resolvedPath(path));
}

// Looks at each folder of a path for a link or junction.
bool hasLinkOnPath(const fs::path& path)
{
    const fs::path normal = normalFolder(path);
    fs::path part = normal.root_path();
    for (const fs::path& name : normal.relative_path()) {
        part /= name;
        const DWORD attributes = GetFileAttributesW(part.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD error = GetLastError();
            return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
        }
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return true;
    }
    return false;
}

// Reads the attributes of one entry.
unsigned long attributesOf(const fs::path& path)
{
    return GetFileAttributesW(longPath(path).c_str());
}

// Adds the long path prefix when a path would be too long without it.
std::wstring longPath(const fs::path& path)
{
    std::wstring text = path.wstring();
    if (text.size() < 240 || text.rfind(L"\\\\?\\", 0) == 0) return text;
    if (text.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + text.substr(2);
    return L"\\\\?\\" + text;
}

}

namespace {

using blenderpaths::environmentText;

// What runProcess says when programs a run started could not all be ended.
constexpr std::string_view kProgramsLeftMessage = "Programs that the program started could not all be ended.";

// The folder an environment variable names, as an absolute path, when that folder exists. Empty otherwise.
fs::path existingFolderFrom(const wchar_t* name)
{
    const std::wstring text = environmentText(name);
    if (text.empty()) return {};
    std::error_code ec;
    const fs::path path(text);
    if (!fs::is_directory(path, ec)) return {};
    return blenderpaths::normalFolder(fs::absolute(path, ec));
}

// Reads the thread cap. threads is empty when none is set.
Status blenderThreads(std::string& threads)
{
    threads.clear();
    const std::wstring name(kBlenderThreadsVariable.begin(), kBlenderThreadsVariable.end());
    const std::wstring text = environmentText(name.c_str());
    if (text.empty()) return {};
    const bool digits = text.size() <= 4 && text.front() >= L'1' && text.front() <= L'9' &&
                        std::all_of(text.begin(), text.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
    const int value = digits ? std::stoi(text) : 0;
    if (!digits || value > 1024) {
        return fail(ErrorCode::InvalidValue,
                    std::string(kBlenderThreadsVariable) + " must be a whole number from 1 to 1024, so Blender was not started.");
    }
    threads = std::to_string(value);
    return {};
}

// Checks a job's own extensions folder by the rules in Blender.h, before anything is made or started.
Status checkOwnExtensions(const fs::path& folder, bool factoryStartup)
{
    const auto refused = [&](const std::string& why) {
        return fail(ErrorCode::InvalidValue, "The extensions folder " + pathToUtf8(folder) + " was not used, because " + why);
    };
    if (!factoryStartup) return refused("a job with its own extensions folder needs factory startup.");
    if (folder.empty() || !folder.is_absolute()) return refused("it is not an absolute path.");
    if (blenderpaths::namesBlenderFoundation(folder)) return refused("its path names the Blender Foundation folder.");
    const fs::path resolved = blenderpaths::resolvedPath(folder);
    if (resolved.empty()) return refused("it could not be found.");
    const DWORD marker = blenderpaths::attributesOf(resolved / pathFromUtf8(kPreparedExtensionsMarker));
    if (marker == INVALID_FILE_ATTRIBUTES || (marker & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        return refused("it was not prepared for one job.");
    }
    std::error_code ec;
    std::vector<fs::path> kept{fs::absolute(blenderpaths::privateFolder(), ec)};
    if (const std::wstring own = environmentText(L"BLENDER_USER_EXTENSIONS"); !own.empty()) {
        kept.push_back(fs::absolute(fs::path(own), ec));
    }
    if (const std::wstring app = environmentText(L"APPDATA"); !app.empty()) {
        kept.push_back(fs::absolute(fs::path(app) / L"Blender Foundation", ec));
    }
    for (const fs::path& other : kept) {
        if (blenderpaths::overlaps(folder, other)) return refused("it is in or around " + pathToUtf8(other) + ".");
    }
    return {};
}

// Creates the private folder and sets the variables that send a factory startup Blender there, replacing any
// that point at the user's folders. Only the child sees them. Fails when the folder cannot be created, so a
// run never falls back to the user's folders.
Status useBlenderPrivateFolder(ProcessSpec& spec)
{
    const fs::path root = blenderpaths::privateFolder();
    std::error_code ec;
    for (const wchar_t* sub : {L"", L"config", L"scripts", L"extensions", L"datafiles"}) {
        fs::create_directories(root / sub, ec);
        if (ec) return fail(ErrorCode::CannotWrite, "The private Blender folder " + pathToUtf8(root) + " could not be created.");
    }
    spec.environment.emplace_back("BLENDER_USER_RESOURCES", pathToUtf8(root));
    spec.environment.emplace_back("BLENDER_USER_CONFIG", pathToUtf8(root / L"config"));
    spec.environment.emplace_back("BLENDER_USER_SCRIPTS", pathToUtf8(root / L"scripts"));
    spec.environment.emplace_back("BLENDER_USER_EXTENSIONS", pathToUtf8(root / L"extensions"));
    spec.environment.emplace_back("BLENDER_USER_DATAFILES", pathToUtf8(root / L"datafiles"));
    return {};
}

// A path as a lower case string, so the same install is found once however it was spelled.
std::wstring pathKey(const fs::path& path)
{
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(path, ec);
    if (ec) canonical = path.lexically_normal();
    std::wstring key = canonical.wstring();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return key;
}

// Collects candidates in the order they are found, merging the same path found twice.
class Candidates {
public:
    // Adds a blender.exe when it exists.
    void add(const fs::path& exe, BlenderSource source)
    {
        std::error_code ec;
        if (exe.empty() || !fs::exists(exe, ec)) return;
        if (fs::is_directory(exe, ec)) return;
        const std::wstring key = pathKey(exe);
        auto found = index_.find(key);
        if (found == index_.end()) {
            fs::path shown = fs::weakly_canonical(exe, ec);
            if (ec) shown = fs::absolute(exe);
            BlenderCandidate c;
            c.executable = shown;
            index_[key] = list_.size();
            list_.push_back(std::move(c));
            found = index_.find(key);
        }
        auto& sources = list_[found->second].sources;
        if (std::find(sources.begin(), sources.end(), source) == sources.end()) sources.push_back(source);
    }

    // Adds folder/blender.exe and folder/*/blender.exe.
    void addFolder(const fs::path& folder, BlenderSource source)
    {
        add(folder / L"blender.exe", source);
        std::error_code ec;
        std::vector<fs::path> subs;
        for (const auto& entry : fs::directory_iterator(folder, ec)) {
            if (entry.is_directory(ec)) subs.push_back(entry.path());
        }
        std::sort(subs.begin(), subs.end());
        for (const fs::path& sub : subs) add(sub / L"blender.exe", source);
    }

    std::vector<BlenderCandidate> take() { return std::move(list_); }

private:
    std::vector<BlenderCandidate> list_;
    std::map<std::wstring, std::size_t> index_;
};

// How long reading a version may take. PLYNTH_BLENDER_VERSION_TIMEOUT_MS shortens it for tests.
std::chrono::milliseconds versionTimeout()
{
    const std::wstring text = environmentText(L"PLYNTH_BLENDER_VERSION_TIMEOUT_MS");
    if (!text.empty()) {
        const long long ms = _wtoll(text.c_str());
        if (ms > 0) return std::chrono::milliseconds(ms);
    }
    return kBlenderVersionTimeout;
}

// The last lines of some text, for an error message.
std::string lastLines(const std::string& text, int count)
{
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    std::string out;
    const std::size_t start = lines.size() > static_cast<std::size_t>(count) ? lines.size() - static_cast<std::size_t>(count) : 0;
    for (std::size_t i = start; i < lines.size(); ++i) {
        if (!out.empty()) out += "\n";
        out += lines[i];
    }
    return out;
}

// Reads which step a progress line names and builds the line again from the list, so none of its own text is
// kept. Empty when the line is not a progress line or its step is not listed.
std::optional<BlenderProgress> rebuiltStep(std::string_view line, const std::vector<BlenderStep>& steps)
{
    if (steps.empty() || line.rfind(kProgressPrefix, 0) != 0) return std::nullopt;
    const Json parsed = Json::parse(line.substr(kProgressPrefix.size()), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object() || !parsed.contains("progress") || !parsed["progress"].is_object()) {
        return std::nullopt;
    }
    const Json& p = parsed["progress"];
    if (!p.contains("step") || !p["step"].is_string()) return std::nullopt;
    const std::string& step = p["step"].get_ref<const std::string&>();
    for (std::size_t i = 0; i < steps.size(); ++i) {
        if (steps[i].step != step) continue;
        BlenderProgress progress;
        progress.step = steps[i].step;
        progress.index = static_cast<int>(i) + 1;
        progress.count = static_cast<int>(steps.size());
        progress.message = steps[i].message;
        return progress;
    }
    return std::nullopt;
}

}

// "5.2.0".
std::string formatBlenderVersion(BlenderVersion v)
{
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

// Reads the first version number in a text.
std::optional<BlenderVersion> parseBlenderVersion(std::string_view text)
{
    std::size_t i = 0;
    const auto number = [&](int& out) {
        if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) return false;
        long long value = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            value = value * 10 + (text[i] - '0');
            if (value > 100000) return false;
            ++i;
        }
        out = static_cast<int>(value);
        return true;
    };
    while (i < text.size()) {
        while (i < text.size() && !std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
        BlenderVersion v;
        if (number(v.major) && i < text.size() && text[i] == '.') {
            ++i;
            if (number(v.minor)) {
                if (i < text.size() && text[i] == '.') {
                    const std::size_t save = i;
                    ++i;
                    if (!number(v.patch)) {
                        v.patch = 0;
                        i = save;
                    }
                }
                return v;
            }
        }
    }
    return std::nullopt;
}

// How a version relates to the supported range.
BlenderSupport blenderSupport(BlenderVersion version)
{
    if (version < kMinBlenderVersion) return BlenderSupport::TooOld;
    if (version.major > kTestedBlenderVersion.major) return BlenderSupport::Untested;
    return BlenderSupport::Supported;
}

// The name used in answers.
std::string_view blenderSupportName(BlenderSupport support)
{
    switch (support) {
    case BlenderSupport::Supported: return "supported";
    case BlenderSupport::TooOld: return "too_old";
    case BlenderSupport::Untested: return "untested";
    }
    return "supported";
}

// The name used in answers.
std::string_view blenderSourceName(BlenderSource source)
{
    switch (source) {
    case BlenderSource::Settings: return "settings";
    case BlenderSource::Environment: return "environment";
    case BlenderSource::ProgramFolder: return "program_folder";
    case BlenderSource::Registry: return "registry";
    case BlenderSource::Store: return "store";
    case BlenderSource::Steam: return "steam";
    case BlenderSource::SystemPath: return "path";
    case BlenderSource::Extra: return "extra";
    }
    return "extra";
}

// Lists every blender.exe in the places the search names.
std::vector<BlenderCandidate> findBlenderCandidates(const BlenderSearch& search)
{
    Candidates found;
    if (search.chosen) found.add(*search.chosen, BlenderSource::Settings);
    if (search.environment) {
        if (const std::wstring env = environmentText(L"PLYNTH_BLENDER"); !env.empty()) {
            std::error_code ec;
            const fs::path path(env);
            if (fs::is_directory(path, ec)) found.add(path / L"blender.exe", BlenderSource::Environment);
            else found.add(path, BlenderSource::Environment);
        }
    }
    if (search.programFolders) {
        for (const wchar_t* variable : {L"ProgramFiles", L"ProgramW6432", L"ProgramFiles(x86)"}) {
            if (const std::wstring base = environmentText(variable); !base.empty()) {
                found.addFolder(fs::path(base) / L"Blender Foundation", BlenderSource::ProgramFolder);
            }
        }
        if (const std::wstring local = environmentText(L"LOCALAPPDATA"); !local.empty()) {
            found.addFolder(fs::path(local) / L"Programs" / L"Blender Foundation", BlenderSource::ProgramFolder);
        }
    }
    if (search.registry) {
        for (const fs::path& p : discovery::registryBlenders()) found.add(p, BlenderSource::Registry);
    }
    if (search.store) {
        for (const fs::path& p : discovery::storeBlenders()) found.add(p, BlenderSource::Store);
    }
    if (search.steam) {
        for (const fs::path& p : discovery::steamBlenders()) found.add(p, BlenderSource::Steam);
    }
    if (search.systemPath) {
        const std::wstring path = environmentText(L"PATH");
        std::size_t start = 0;
        while (start <= path.size()) {
            std::size_t end = path.find(L';', start);
            if (end == std::wstring::npos) end = path.size();
            const std::wstring folder = path.substr(start, end - start);
            if (!folder.empty()) found.add(fs::path(folder) / L"blender.exe", BlenderSource::SystemPath);
            start = end + 1;
        }
    }
    for (const fs::path& folder : search.extraFolders) found.addFolder(folder, BlenderSource::Extra);
    return found.take();
}

// Runs blender --version and reads the answer.
BlenderInstall inspectBlender(const fs::path& executable, std::stop_token stop)
{
    BlenderInstall install;
    install.executable = executable;

    ProcessSpec spec;
    spec.program = executable;
    spec.arguments = {"--background", "--disable-autoexec", "--factory-startup", "--version"};
    spec.timeout = versionTimeout();
    if (const Status s = useBlenderPrivateFolder(spec); !s.ok()) {
        install.status = fail(ErrorCode::BlenderFailed, s.message);
        return install;
    }
    std::vector<std::string> lines;
    const ProcessResult run = runProcess(spec, [&](std::string_view line) { lines.emplace_back(line); }, stop);

    if (!run.status.ok()) {
        if (run.timedOut) {
            install.status = fail(ErrorCode::BlenderFailed,
                                  "Blender did not answer within " + std::to_string(spec.timeout.count() / 1000) + " seconds.");
        } else {
            install.status = fail(ErrorCode::BlenderFailed, run.status.message);
        }
        return install;
    }
    for (const std::string& line : lines) {
        if (line.rfind("Blender", 0) != 0) continue;
        if (const auto v = parseBlenderVersion(line)) {
            install.version = v;
            install.versionText = line;
            break;
        }
    }
    if (!install.version) {
        install.status = fail(ErrorCode::BlenderFailed, "Blender started but did not report a version." +
                                                           (run.outputTail.empty() ? std::string() : "\n" + lastLines(run.outputTail, 3)));
        return install;
    }
    install.support = blenderSupport(*install.version);
    return install;
}

// Finds the candidates, inspects each and sorts them newest first.
std::vector<BlenderInstall> findBlenderInstalls(const BlenderSearch& search, std::stop_token stop)
{
    std::vector<BlenderInstall> installs;
    for (const BlenderCandidate& c : findBlenderCandidates(search)) {
        if (stop.stop_requested()) break;
        BlenderInstall install = inspectBlender(c.executable, stop);
        install.sources = c.sources;
        installs.push_back(std::move(install));
    }
    std::stable_sort(installs.begin(), installs.end(), [](const BlenderInstall& a, const BlenderInstall& b) {
        if (a.version.has_value() != b.version.has_value()) return a.version.has_value();
        if (a.version && b.version && *a.version != *b.version) return *a.version > *b.version;
        return a.executable < b.executable;
    });
    return installs;
}

// The install to preselect.
std::optional<std::size_t> preferredBlender(const std::vector<BlenderInstall>& installs,
                                            const std::optional<fs::path>& chosen)
{
    if (chosen) {
        const std::wstring key = pathKey(*chosen);
        for (std::size_t i = 0; i < installs.size(); ++i) {
            if (installs[i].usable() && pathKey(installs[i].executable) == key) return i;
        }
    }
    for (const BlenderSupport wanted : {BlenderSupport::Supported, BlenderSupport::Untested}) {
        for (std::size_t i = 0; i < installs.size(); ++i) {
            if (installs[i].usable() && installs[i].support == wanted) return i;
        }
    }
    return std::nullopt;
}

// Works out where the user's own Blender keeps its files, without touching them.
BlenderUserFolders blenderUserFolders(const BlenderInstall& install)
{
    std::error_code ec;
    const fs::path resources = existingFolderFrom(L"BLENDER_USER_RESOURCES");
    fs::path portable;
    if (install.executable.has_parent_path()) {
        const fs::path beside = install.executable.parent_path() / L"portable";
        if (fs::is_directory(beside, ec)) portable = blenderpaths::normalFolder(fs::absolute(beside, ec));
    }
    fs::path appData;
    if (install.version) {
        if (const std::wstring base = environmentText(L"APPDATA"); !base.empty()) {
            appData = blenderpaths::normalFolder(
                fs::absolute(fs::path(base) / L"Blender Foundation" / L"Blender" /
                                 (std::to_wstring(install.version->major) + L"." + std::to_wstring(install.version->minor)),
                             ec));
        }
    }
    const auto pick = [&](const wchar_t* variable, const wchar_t* name) -> fs::path {
        if (fs::path own = existingFolderFrom(variable); !own.empty()) return own;
        for (const fs::path& base : {resources, portable, appData}) {
            if (!base.empty()) return base / name;
        }
        return {};
    };

    BlenderUserFolders folders;
    folders.config = pick(L"BLENDER_USER_CONFIG", L"config");
    folders.scripts = pick(L"BLENDER_USER_SCRIPTS", L"scripts");
    folders.extensions = pick(L"BLENDER_USER_EXTENSIONS", L"extensions");
    folders.datafiles = pick(L"BLENDER_USER_DATAFILES", L"datafiles");
    for (const wchar_t* variable : {L"XDG_DATA_HOME", L"USERPROFILE"}) {
        if (const std::wstring base = environmentText(variable); !base.empty()) {
            folders.blenderKitData = blenderpaths::normalFolder(fs::absolute(fs::path(base) / L"blenderkit_data", ec));
            break;
        }
    }
    return folders;
}

// Picks the Blender for a job.
BlenderChoice chooseBlender(const std::optional<fs::path>& option, const MachineSettings& settings, std::stop_token stop)
{
    BlenderChoice choice;
    const auto problem = [](const BlenderInstall& install) {
        if (!install.status.ok()) return install.status.message;
        if (install.support == BlenderSupport::TooOld) {
            return "Blender " + formatBlenderVersion(*install.version) + " is older than " + formatBlenderVersion(kMinBlenderVersion) + ".";
        }
        return std::string("It cannot be used.");
    };

    if (option) {
        // An empty path was still asked for on purpose, so it never means "find one".
        if (option->empty()) {
            choice.status = fail(ErrorCode::BlenderNotFound, "No path was given for Blender, so no Blender was started.");
            return choice;
        }
        std::error_code ec;
        if (!fs::is_regular_file(*option, ec)) {
            choice.status = fail(ErrorCode::BlenderNotFound, "The Blender " + pathToUtf8(*option) + " does not exist.");
            return choice;
        }
        BlenderInstall install = inspectBlender(*option, stop);
        if (!install.usable()) {
            choice.status = fail(ErrorCode::BlenderNotFound, "The Blender " + pathToUtf8(*option) + " cannot be used. " + problem(install));
            return choice;
        }
        choice.install = std::move(install);
        return choice;
    }

    std::string tried;
    const auto tryPath = [&](const fs::path& path, const std::string& where) {
        std::error_code ec;
        if (path.empty() || !fs::is_regular_file(path, ec)) {
            tried += " " + where + " " + pathToUtf8(path) + " does not exist.";
            return false;
        }
        BlenderInstall install = inspectBlender(path, stop);
        if (!install.usable()) {
            tried += " " + where + " " + pathToUtf8(path) + " cannot be used: " + problem(install);
            return false;
        }
        choice.install = std::move(install);
        return true;
    };

    if (const std::wstring env = environmentText(L"PLYNTH_BLENDER"); !env.empty()) {
        std::error_code ec;
        fs::path path(env);
        if (fs::is_directory(path, ec)) path /= L"blender.exe";
        if (tryPath(path, "PLYNTH_BLENDER")) return choice;
        choice.status = fail(ErrorCode::BlenderNotFound, "The Blender named by PLYNTH_BLENDER cannot be used." + tried);
        return choice;
    }
    if (settings.blender && tryPath(*settings.blender, "The Blender in the settings")) return choice;

    BlenderSearch search;
    search.chosen = settings.blender;
    const std::vector<BlenderInstall> installs = findBlenderInstalls(search, stop);
    if (const auto index = preferredBlender(installs, settings.blender)) {
        choice.install = installs[*index];
        return choice;
    }
    if (installs.empty()) {
        choice.status = fail(ErrorCode::BlenderNotFound,
                             "Blender was not found. Install Blender " + formatBlenderVersion(kMinBlenderVersion) +
                                 " or newer from blender.org, or pick blender.exe in the setup check." + tried);
    } else {
        std::string found;
        for (const BlenderInstall& i : installs) found += " " + pathToUtf8(i.executable) + ": " + problem(i);
        choice.status = fail(ErrorCode::BlenderNotFound, "No usable Blender was found." + found + tried);
    }
    return choice;
}

// Looks for the Plynth scripts.
fs::path blenderScriptsFolder()
{
    const auto holdsWorker = [](const fs::path& folder) {
        std::error_code ec;
        return !folder.empty() && fs::is_regular_file(folder / pathFromUtf8(kWorkerScriptName), ec);
    };
    if (const std::wstring env = environmentText(L"PLYNTH_BLENDER_SCRIPTS"); !env.empty()) {
        if (holdsWorker(fs::path(env))) return fs::path(env);
    }
    wchar_t program[MAX_PATH];
    const DWORD got = GetModuleFileNameW(nullptr, program, static_cast<DWORD>(std::size(program)));
    if (got > 0 && got < std::size(program)) {
        const fs::path beside = fs::path(program).parent_path() / L"blender";
        if (holdsWorker(beside)) return beside;
    }
#ifdef PLYNTH_SOURCE_BLENDER_DIR
    {
        const fs::path source(PLYNTH_SOURCE_BLENDER_DIR);
        if (holdsWorker(source)) return source;
    }
#endif
    return {};
}

// Runs one job in Blender.
BlenderJobRun runBlenderJob(const fs::path& blender, const Json& job, const fs::path& workFolder,
                            const BlenderRunOptions& options, const std::function<void(const BlenderProgress&)>& onProgress,
                            std::stop_token stop)
{
    BlenderJobRun run;
    std::string threads;
    if (Status s = blenderThreads(threads); !s.ok()) {
        run.status = s;
        return run;
    }
    // The probe reads the user's saved state as data, so it never gets the user's folders, whatever was asked.
    const bool isProbe = job.contains("task") && job["task"].is_string() && job["task"].get<std::string>() == kProbeTask;
    fs::path ownExtensions;
    if (!isProbe && !options.extensionsFolder.empty()) {
        if (Status s = checkOwnExtensions(options.extensionsFolder, options.factoryStartup); !s.ok()) {
            run.status = s;
            return run;
        }
        ownExtensions = blenderpaths::resolvedPath(options.extensionsFolder);
    }
    const fs::path scripts = options.scriptsFolder.empty() ? blenderScriptsFolder() : options.scriptsFolder;
    std::error_code ec;
    if (scripts.empty() || !fs::is_regular_file(scripts / pathFromUtf8(kWorkerScriptName), ec)) {
        run.status = fail(ErrorCode::NotFound, "The Blender scripts folder with " + std::string(kWorkerScriptName) + " was not found.");
        return run;
    }
    const bool fetch = !options.privateFolder.empty();
    const fs::path guard = scripts / pathFromUtf8(kFetchGuardFolder);
    if (fetch && !fs::is_regular_file(guard / pathFromUtf8(kFetchGuardFile), ec)) {
        run.status = fail(ErrorCode::NotFound, "The Blender scripts folder has no store fetch guard, so the fetch was not started.");
        return run;
    }
    fs::create_directories(workFolder, ec);
    if (ec || !fs::is_directory(workFolder, ec)) {
        run.status = fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(workFolder) + " could not be created.");
        return run;
    }
    const fs::path jobFile = workFolder / pathFromUtf8(kJobFileName);
    const std::string resultName = job.contains("result_file") && job["result_file"].is_string() ? job["result_file"].get<std::string>() : "";
    const fs::path resultFile = pathFromUtf8(resultName);
    if (Status s = fileio::writeAtomic(jobFile, job.dump(2, ' ', false, Json::error_handler_t::replace) + "\n"); !s.ok()) {
        run.status = s;
        return run;
    }
    if (!resultName.empty()) fs::remove(resultFile, ec);

    ProcessSpec spec;
    spec.program = blender;
    // Scripts stored inside .blend files never run: packed assets can come from other people (Pack.h).
    spec.arguments = {"--background", "--disable-autoexec"};
    // Without factory startup the user's add-ons run, and what they print may hold their secrets.
    const bool trusted = options.factoryStartup || isProbe;
    if (trusted) {
        // Offline mode goes with every factory startup, so a copied add-on never goes online.
        spec.arguments.emplace_back("--factory-startup");
        spec.arguments.emplace_back("--offline-mode");
        if (const Status s = useBlenderPrivateFolder(spec); !s.ok()) {
            run.status = s;
            return run;
        }
        if (!ownExtensions.empty()) {
            for (auto& [name, value] : spec.environment) {
                if (name == "BLENDER_USER_EXTENSIONS") value = pathToUtf8(ownExtensions);
            }
        }
    }
    if (!threads.empty()) {
        spec.arguments.emplace_back("-t");
        spec.arguments.push_back(threads);
    }
    for (const char* arg : {"-noaudio", "--python-exit-code", "3", "--python"}) spec.arguments.emplace_back(arg);
    spec.arguments.push_back(pathToUtf8(scripts / pathFromUtf8(kWorkerScriptName)));
    for (const char* arg : {"--", "--job"}) spec.arguments.emplace_back(arg);
    spec.arguments.push_back(pathToUtf8(jobFile));
    spec.timeout = std::chrono::duration_cast<std::chrono::milliseconds>(options.timeout);
    // A helper Blender starts, such as a store's download program, must be gone before the caller goes on.
    spec.endChildren = true;
    if (fetch) {
        const fs::path temp = options.privateFolder / pathFromUtf8(kFetchTempFolder);
        fs::create_directories(temp, ec);
        if (ec) {
            run.status = fail(ErrorCode::CannotWrite, "Plynth's fetch folder could not be prepared.");
            return run;
        }
        // At low integrity Windows refuses every write outside the fetch folder, whatever the add-ons do.
        spec.lowIntegrity = true;
        for (const std::string_view name : kFetchRemovedVariables) spec.removeEnvironment.emplace_back(name);
        // Python looks at TMPDIR before TEMP, so an inherited TMPDIR would point its temp folder elsewhere.
        spec.removeEnvironment.emplace_back("TMPDIR");
        spec.environment.emplace_back("TEMP", pathToUtf8(temp));
        spec.environment.emplace_back("TMP", pathToUtf8(temp));
        spec.environment.emplace_back(std::string(kFetchPrivateVariable), pathToUtf8(options.privateFolder));
        spec.environment.emplace_back("PYTHONDONTWRITEBYTECODE", "1");
        // The guard folder comes first, so its startup script runs before any add-on registers.
        std::string system = pathToUtf8(guard);
        if (const std::wstring given = environmentText(L"BLENDER_SYSTEM_SCRIPTS"); !given.empty()) {
            system += ";" + pathToUtf8(fs::path(given));
        }
        spec.environment.emplace_back("BLENDER_SYSTEM_SCRIPTS", system);
    }

    constexpr std::size_t kMaxLogBytes = 1024 * 1024;
    std::string log;
    std::string lastStep;
    const auto onLine = [&](std::string_view line) {
        if (!trusted) {
            const std::optional<BlenderProgress> progress = rebuiltStep(line, options.steps);
            if (!progress || progress->step == lastStep) return;
            lastStep = progress->step;
            log += "Step " + std::to_string(progress->index) + " of " + std::to_string(progress->count) + ", " +
                   progress->step + ": " + progress->message + "\n";
            if (onProgress) onProgress(*progress);
            return;
        }
        log.append(line);
        log.push_back('\n');
        if (log.size() > 2 * kMaxLogBytes) log.erase(0, log.size() - kMaxLogBytes);
        if (line.rfind(kProgressPrefix, 0) != 0 || !onProgress) return;
        const Json parsed = Json::parse(line.substr(kProgressPrefix.size()), nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object() || !parsed.contains("progress") || !parsed["progress"].is_object()) return;
        const Json& p = parsed["progress"];
        BlenderProgress progress;
        if (p.contains("step") && p["step"].is_string()) progress.step = p["step"].get<std::string>();
        if (p.contains("index") && p["index"].is_number_integer()) progress.index = p["index"].get<int>();
        if (p.contains("count") && p["count"].is_number_integer()) progress.count = p["count"].get<int>();
        if (p.contains("message") && p["message"].is_string()) progress.message = p["message"].get<std::string>();
        onProgress(progress);
    };
    ProcessResult process = runProcess(spec, onLine, stop);
    if (!trusted) process.outputTail.clear();
    // Without factory startup the result file may hold what the add-ons wrote, so it never stays on disk.
    const auto dropResult = [&] {
        if (!trusted && !resultName.empty()) fs::remove(resultFile, ec);
    };

    {
        std::ofstream out(workFolder / pathFromUtf8(kBlenderLogFileName), std::ios::binary | std::ios::trunc);
        out << log;
    }
    constexpr std::size_t kLogTail = 4096;
    run.logTail = log.size() > kLogTail ? log.substr(log.size() - kLogTail) : log;
    run.exitCode = process.exitCode;
    if (process.status.code != ErrorCode::None) dropResult();

    // A helper that is still running may change the result, so even a good one is not used.
    if (process.status.code == ErrorCode::CannotRead && process.status.message == kProgramsLeftMessage) {
        if (!resultName.empty()) fs::remove(resultFile, ec);
        run.status = fail(ErrorCode::BlenderFailed, "Blender's helper programs could not all be ended.");
        return run;
    }

    if (process.status.code == ErrorCode::NotFound) {
        run.status = fail(ErrorCode::BlenderNotFound, "The Blender " + pathToUtf8(blender) + " does not exist.");
        return run;
    }
    if (process.timedOut) {
        run.status = fail(ErrorCode::BlenderFailed, "Blender timed out after " + std::to_string(options.timeout.count()) +
                                                        " seconds and was stopped.");
        return run;
    }
    if (process.status.code == ErrorCode::Cancelled) {
        run.status = fail(ErrorCode::Cancelled, "The job was cancelled.");
        return run;
    }
    if (!process.status.ok()) {
        run.status = fail(ErrorCode::BlenderFailed, process.status.message);
        return run;
    }

    const std::string tail = trusted ? lastLines(log, 8) : std::string();
    const auto failedWith = [&](const std::string& why) {
        run.status = fail(ErrorCode::BlenderFailed, why + (tail.empty() ? std::string() : "\n" + tail));
    };
    std::string text;
    bool haveResult = false;
    if (!resultName.empty()) {
        std::ifstream in(resultFile, std::ios::binary);
        if (in) {
            std::ostringstream read;
            read << in.rdbuf();
            text = read.str();
            haveResult = true;
        }
    }
    dropResult();
    if (!haveResult) {
        failedWith("Blender ended (exit code " + std::to_string(process.exitCode) + ") without writing a result.");
        return run;
    }
    Json result = Json::parse(text, nullptr, false);
    if (result.is_discarded() || !result.is_object() || !result.contains("ok") || !result["ok"].is_boolean()) {
        failedWith("Blender wrote a result that could not be read.");
        return run;
    }
    run.result = std::move(result);
    if (!run.result["ok"].get<bool>()) {
        std::string message = "The job failed.";
        if (run.result.contains("error") && run.result["error"].is_object()) {
            const Json& error = run.result["error"];
            if (error.contains("code") && error["code"].is_string()) run.errorCode = error["code"].get<std::string>();
            if (error.contains("message") && error["message"].is_string()) message = error["message"].get<std::string>();
        }
        run.status = fail(ErrorCode::BlenderFailed, message);
    }
    return run;
}

}
