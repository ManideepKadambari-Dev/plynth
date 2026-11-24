#include "BuildFixtures.h"
#include "FakeBlender.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/Addons.h>
#include <plynth/core/Blender.h>
#include <plynth/core/BlenderJob.h>
#include <plynth/core/Build.h>
#include <plynth/core/EditSession.h>
#include <plynth/core/GeneratorBuild.h>
#include <plynth/core/Process.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <regex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace plynth;
using namespace plynth::test;
namespace fs = std::filesystem;

namespace {

const std::string kAddon = "plynth_test_addon";
const std::string kMixed = "Plynth_Mixed";
const std::string kRepository = "blender_org";
const std::string kMarker{kPreparedExtensionsMarker};

using Names = std::vector<std::string>;
using EnvList = std::vector<std::unique_ptr<ScopedEnv>>;

// The module name of an extension.
std::string moduleOf(const std::string& id, const std::string& repository = kRepository)
{
    return "bl_ext." + repository + "." + id;
}

// The value of an environment variable.
fs::path variablePath(const std::string& name)
{
    const std::wstring wide = widenUtf8(name);
    const DWORD size = GetEnvironmentVariableW(wide.c_str(), nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    value.resize(GetEnvironmentVariableW(wide.c_str(), value.data(), size));
    return fs::path(value);
}

// A path in one spelling, worked out from its text alone.
std::wstring spelling(const fs::path& path)
{
    std::wstring text = path.lexically_normal().generic_wstring();
    while (!text.empty() && text.back() == L'/') text.pop_back();
    for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
    return text;
}

// Says whether a path lies below a folder, by the text alone.
bool insideFolder(const fs::path& path, const fs::path& folder)
{
    if (path.empty() || folder.empty() || !path.is_absolute()) return false;
    const std::wstring outer = spelling(folder) + L"/";
    return spelling(path).rfind(outer, 0) == 0;
}

// Writes a file, making its folder.
void writeText(const fs::path& file, const std::string& text)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
}

// Writes a small add-on made for these tests and returns its folder.
fs::path writeAddon(const fs::path& extensions, const std::string& id = kAddon,
                    const std::string& repository = kRepository)
{
    const fs::path folder = extensions / repository / id;
    writeText(folder / "blender_manifest.toml",
              "schema_version = \"1.0.0\"\nid = \"" + id + "\"\nversion = \"1.0.0\"\nname = \"Plynth test add-on\"\n"
              "tagline = \"Made for the Plynth tests\"\nmaintainer = \"Plynth tests\"\ntype = \"add-on\"\n"
              "blender_version_min = \"4.2.0\"\nlicense = [\"SPDX:GPL-3.0-or-later\"]\n");
    writeText(folder / "__init__.py", "def register():\n    pass\n\n\ndef unregister():\n    pass\n");
    writeText(folder / "notes.txt", "from " + repository + "\n");
    return folder;
}

// Holds a file or folder open so that any other open to read it fails, or only one to write it.
class HeldOpen {
public:
    explicit HeldOpen(const fs::path& path, DWORD share = 0)
    {
        for (int attempt = 0; attempt < 40; ++attempt) {
            handle_ = CreateFileW(path.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (handle_ != INVALID_HANDLE_VALUE) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        FAIL("Could not hold " << pathToUtf8(path));
    }
    ~HeldOpen() { CloseHandle(handle_); }
    HeldOpen(const HeldOpen&) = delete;
    HeldOpen& operator=(const HeldOpen&) = delete;

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

using HeldList = std::vector<std::unique_ptr<HeldOpen>>;

// Holds a folder and everything in it, without going through links.
void holdAll(HeldList& held, const fs::path& folder)
{
    std::vector<fs::path> paths{folder};
    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        if (fs::is_regular_file(entry.symlink_status()) || fs::is_directory(entry.symlink_status())) paths.push_back(entry.path());
    }
    for (auto it = paths.rbegin(); it != paths.rend(); ++it) held.push_back(std::make_unique<HeldOpen>(*it));
}

// A junction to a folder, removed with the object without touching what it points at.
class DirectoryLink {
public:
    DirectoryLink(const fs::path& link, const fs::path& target) : link_(link)
    {
        fs::create_directories(link.parent_path());
        fs::create_directory(link);
        fs::path shown = fs::absolute(target).lexically_normal();
        shown.make_preferred();
        const std::wstring print = shown.wstring();
        const std::wstring real = L"\\??\\" + print;
        const WORD realBytes = static_cast<WORD>(real.size() * sizeof(wchar_t));
        const WORD printBytes = static_cast<WORD>(print.size() * sizeof(wchar_t));

        std::vector<BYTE> data(16 + realBytes + 2 + printBytes + 2, 0);
        const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
        const WORD fields[] = {static_cast<WORD>(data.size() - 8), 0, 0, realBytes, static_cast<WORD>(realBytes + 2), printBytes};
        std::memcpy(data.data(), &tag, sizeof tag);
        std::memcpy(data.data() + 4, fields, sizeof fields);
        std::memcpy(data.data() + 16, real.data(), realBytes);
        std::memcpy(data.data() + 16 + realBytes + 2, print.data(), printBytes);

        const HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                          FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        REQUIRE(handle != INVALID_HANDLE_VALUE);
        DWORD returned = 0;
        const BOOL made = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, data.data(), static_cast<DWORD>(data.size()),
                                          nullptr, 0, &returned, nullptr);
        CloseHandle(handle);
        REQUIRE(made);
    }
    ~DirectoryLink() { RemoveDirectoryW(link_.c_str()); }
    DirectoryLink(const DirectoryLink&) = delete;
    DirectoryLink& operator=(const DirectoryLink&) = delete;

private:
    fs::path link_;
};

// Every entry below a folder with its attributes, write time and content, without going through links.
Names treeState(const fs::path& folder)
{
    Names out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::string line = pathToUtf8(it->path().lexically_relative(folder));
        line += "|" + std::to_string(GetFileAttributesW(it->path().c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_REPARSE_POINT));
        if (fs::is_regular_file(it->symlink_status())) {
            line += "|" + std::to_string(it->last_write_time().time_since_epoch().count()) + "|" + readTextFile(it->path());
        }
        out.push_back(line);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// A made-up user with an extensions folder and a fake Blender, all in one temporary folder.
struct AddonScratch {
    explicit AddonScratch(const std::string& tag) : user(tag), root(user.tmp.path()), extensions(user.userRoot / "extensions")
    {
        for (const char* name : {"local", "home", "xdg", "settings", "work", "samples/shapes", "samples/generators"}) {
            fs::create_directories(root / name);
        }
        exe = makeFakeBlender(root / "blender", "5.2.0");
        scripts = makeFakeScripts(root / "scripts");
        set("LOCALAPPDATA", root / "local");
        set("HOME", root / "home");
        set("XDG_DATA_HOME", root / "xdg");
        set("PLYNTH_CONFIG_DIR", root / "settings");
        set("PLYNTH_BLENDER", exe);
        set("PLYNTH_BLENDER_SCRIPTS", scripts);
        set("PLYNTH_SAMPLES", root / "samples");
        for (const char* name : {"FAKE_BLENDER_MODE", "FAKE_BLENDER_ADDONS", "FAKE_BLENDER_BLENDERKIT", "PLYNTH_TEST_ADDONS",
                                 "PLYNTH_BLENDER_THREADS"}) {
            set(name, {});
        }
        install.executable = exe;
        install.version = BlenderVersion{5, 2, 0};
        install.support = BlenderSupport::Supported;
    }

    // Points one variable at a path of this test.
    void set(const char* name, const fs::path& value)
    {
        envs.push_back(std::make_unique<ScopedEnv>(name, pathToUtf8(value)));
    }

    // A cache of this Blender that lists these modules as enabled, in this order.
    AddonCache cache(const Names& modules) const
    {
        AddonCache made;
        made.blender = exe;
        made.blenderVersion = "5.2.0";
        made.checkedAt = parseSessionTime("2026-11-22T10:15:00Z").value();
        for (const std::string& module : modules) made.enabled.push_back({module, module, "1.0.0"});
        return made;
    }

    // What every folder a build must leave alone holds.
    Names state() const
    {
        Names all;
        for (const char* name : {"user", "appdata", "profile", "local", "home", "xdg", "resources", "other", "blender/portable"}) {
            all.push_back(std::string("[") + name + "]");
            const Names part = treeState(root / name);
            all.insert(all.end(), part.begin(), part.end());
        }
        return all;
    }

    // Says whether the shared private extensions folder has the marker, which it never may.
    bool sharedMarked() const { return fs::exists(root / "private" / "extensions" / kMarker); }

    IsolatedUser user;
    fs::path root;
    fs::path extensions;
    fs::path exe;
    fs::path scripts;
    BlenderInstall install;
    EnvList envs;
};

// Stops the case unless every folder the code under test could work out lies in its temporary folder. The
// variables are judged first, so no folder outside is looked at.
void requireFakeUser(const AddonScratch& s)
{
    for (const char* name : {"APPDATA", "LOCALAPPDATA", "USERPROFILE", "HOME", "TEMP", "TMP", "PLYNTH_BLENDER_PRIVATE", "PLYNTH_CONFIG_DIR"}) {
        INFO(name);
        REQUIRE(insideFolder(variablePath(name), s.root));
    }
    for (const char* name : {"XDG_DATA_HOME", "BLENDER_USER_RESOURCES", "BLENDER_USER_CONFIG", "BLENDER_USER_SCRIPTS",
                             "BLENDER_USER_EXTENSIONS", "BLENDER_USER_DATAFILES"}) {
        INFO(name);
        const fs::path value = variablePath(name);
        REQUIRE((value.empty() || insideFolder(value, s.root)));
    }
    REQUIRE(insideFolder(s.install.executable, s.root));
    const BlenderUserFolders folders = blenderUserFolders(s.install);
    for (const fs::path& folder : {folders.config, folders.scripts, folders.extensions, folders.blenderKitData}) {
        INFO(pathToUtf8(folder));
        REQUIRE(insideFolder(folder, s.root));
    }
    REQUIRE((folders.datafiles.empty() || insideFolder(folders.datafiles, s.root)));
    REQUIRE(insideFolder(addonCacheFile(), s.root));
}

// Copies after checking that only made-up folders can be reached.
AddonFolderCopy copyAddons(const AddonScratch& s, const fs::path& job, const fs::path& source, const AddonCache& cache,
                           const Names& names, std::stop_token stop = {})
{
    requireFakeUser(s);
    return prepareAddonFolder(job, source, s.install, cache, names, stop);
}

// The usual copy of the test add-on.
AddonFolderCopy copyTestAddon(const AddonScratch& s, const fs::path& job, const fs::path& source)
{
    return copyAddons(s, job, source, s.cache({moduleOf(kAddon)}), {kAddon});
}

// Shows that the same call works from the extensions folder, so a refusal after it means something.
void requireCopyWorks(const AddonScratch& s)
{
    writeAddon(s.extensions);
    const fs::path job = s.user.tempFolder / "control";
    const AddonFolderCopy copy = copyTestAddon(s, job, s.extensions);
    requireOk(copy.status);
    REQUIRE(copy.copied == Names{kAddon});
    fs::remove_all(job);
}

// Checks a refusal that happens before anything is read or made. held is the folder that could be reached.
void requireRefused(const AddonScratch& s, const fs::path& source, const fs::path& held, const AddonCache& cache)
{
    INFO(pathToUtf8(source));
    const Names before = s.state();
    const fs::path job = s.user.tempFolder / "refused";
    AddonFolderCopy copy;
    {
        HeldList holding;
        if (!held.empty() && fs::is_directory(held)) holdAll(holding, held);
        copy = copyAddons(s, job, source, cache, {kAddon});
    }
    REQUIRE(copy.status.code == ErrorCode::InvalidValue);
    REQUIRE_FALSE(copy.status.message.empty());
    REQUIRE(copy.copied.empty());
    REQUIRE_FALSE(fs::exists(job));
    REQUIRE(s.state() == before);
    REQUIRE_FALSE(s.sharedMarked());
}

// The same with a cache that lists the test add-on.
void requireRefused(const AddonScratch& s, const fs::path& source, const fs::path& held)
{
    requireRefused(s, source, held, s.cache({moduleOf(kAddon)}));
}

// Changes the current folder for the life of the object.
struct CurrentFolder {
    explicit CurrentFolder(const fs::path& folder) : before(fs::current_path()) { fs::current_path(folder); }
    ~CurrentFolder()
    {
        std::error_code ec;
        fs::current_path(before, ec);
    }
    CurrentFolder(const CurrentFolder&) = delete;
    CurrentFolder& operator=(const CurrentFolder&) = delete;

    fs::path before;
};

// The short 8.3 spelling of a path, or empty when there is none.
fs::path shortSpelling(const fs::path& path)
{
    const DWORD size = GetShortPathNameW(path.c_str(), nullptr, 0);
    if (size == 0) return {};
    std::wstring text(size, L'\0');
    text.resize(GetShortPathNameW(path.c_str(), text.data(), size));
    return spelling(text) == spelling(path) ? fs::path() : fs::path(text);
}

// The same path in upper case letters a to z.
fs::path upperSpelling(const fs::path& path)
{
    std::wstring text = path.wstring();
    for (wchar_t& c : text) {
        if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
    }
    return text;
}

// The files and folders directly in a folder, by their exact names.
Names namesIn(const fs::path& folder)
{
    Names names;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        names.push_back(pathToUtf8(it->path().filename()));
    }
    std::sort(names.begin(), names.end());
    return names;
}

}

TEST_CASE("the test program keeps every user folder in its own scratch root", "[addons][safety]")
{
    // Nothing here opens or looks for a folder, so it is safe also when the guard is not there.
    const fs::path temp = variablePath("TEMP").lexically_normal();
    const fs::path root = (temp.has_filename() ? temp : temp.parent_path()).parent_path();
    // The setup names the root after this process, so the parent of some other temp folder never passes.
    const std::string rootName = pathToUtf8(root.filename());
    INFO(rootName);
    REQUIRE(std::regex_match(rootName, std::regex("plynth-" + std::to_string(_getpid()) + "-(0|[1-9][0-9]*)")));
    for (const char* name : {"APPDATA", "LOCALAPPDATA", "USERPROFILE", "HOME", "XDG_DATA_HOME", "PLYNTH_BLENDER_PRIVATE",
                             "PLYNTH_CONFIG_DIR", "BLENDER_USER_RESOURCES", "BLENDER_USER_CONFIG", "BLENDER_USER_SCRIPTS",
                             "BLENDER_USER_EXTENSIONS", "BLENDER_USER_DATAFILES", "TMP"}) {
        INFO(name);
        REQUIRE(insideFolder(variablePath(name), root));
    }
    BlenderInstall install;
    install.executable = root / "blender" / "blender.exe";
    install.version = BlenderVersion{5, 2, 0};
    const BlenderUserFolders folders = blenderUserFolders(install);
    for (const fs::path& folder : {folders.config, folders.scripts, folders.extensions, folders.blenderKitData}) {
        REQUIRE(insideFolder(folder, root));
    }
    REQUIRE((folders.datafiles.empty() || insideFolder(folders.datafiles, root)));
}

TEST_CASE("a folder beside the fake user folders is no source", "[addons][addonfolder][safety]")
{
    for (const char* place : {"work/beside", "user/extensions_copy", "appdata/Blender Foundation copy", "user/ext"}) {
        DYNAMIC_SECTION(place)
        {
            AddonScratch s("addonfolder-beside");
            requireCopyWorks(s);
            writeAddon(s.root / place);
            requireRefused(s, s.root / place, s.root / place);
        }
    }
}

TEST_CASE("add-ons are copied from the extensions folder however it is found", "[addons][addonfolder][safety]")
{
    struct Way {
        const char* name;
        const char* folder;
    };
    for (const Way& way : {Way{"variable", "user/extensions"}, Way{"resources", "resources/extensions"},
                           Way{"portable", "blender/portable/extensions"},
                           Way{"appdata", "appdata/Blender Foundation/Blender/5.2/extensions"}}) {
        DYNAMIC_SECTION(way.name)
        {
            AddonScratch s("addonfolder-way");
            const std::string found = way.name;
            EnvList changed;
            const auto point = [&](const char* name, const fs::path& value) {
                changed.push_back(std::make_unique<ScopedEnv>(name, pathToUtf8(value)));
            };
            if (found != "variable") {
                for (const char* name : {"BLENDER_USER_EXTENSIONS", "BLENDER_USER_CONFIG", "BLENDER_USER_SCRIPTS"}) point(name, {});
                point("BLENDER_USER_RESOURCES", found == "resources" ? s.root / "resources" : fs::path());
            }
            const fs::path source = s.root / way.folder;
            writeAddon(source);
            writeAddon(source, "plynth_off");
            writeText(source.parent_path() / "config" / "userpref.blend", "made-up settings");

            HeldList held;
            std::vector<fs::path> decoys;
            for (const char* other : {"user/extensions", "resources/extensions", "blender/portable/extensions",
                                      "appdata/Blender Foundation/Blender/5.2/extensions"}) {
                // A portable folder beside Blender would be chosen over AppData, so that decoy sits beside another one.
                fs::path folder = s.root / other;
                if (found == "appdata" && std::string(other).rfind("blender/", 0) == 0) folder = s.root / "other/portable/extensions";
                if (spelling(folder) == spelling(source)) continue;
                writeAddon(folder);
                decoys.push_back(folder);
            }
            requireFakeUser(s);
            REQUIRE(spelling(blenderUserFolders(s.install).extensions) == spelling(source));
            const Names before = s.state();
            for (const fs::path& folder : decoys) holdAll(held, folder);
            holdAll(held, source.parent_path() / "config");

            const fs::path job = s.user.tempFolder / "job";
            const AddonFolderCopy copy =
                copyAddons(s, job, blenderUserFolders(s.install).extensions, s.cache({moduleOf(kAddon)}), {kAddon, "plynth_off"});
            held.clear();

            requireOk(copy.status);
            REQUIRE(copy.copied == Names{kAddon});
            REQUIRE(namesIn(job / "user_default") == Names{kAddon});
            REQUIRE(fs::is_regular_file(job / kMarker));
            REQUIRE(readTextFile(job / "user_default" / kAddon / "notes.txt") == "from blender_org\n");
            REQUIRE(s.state() == before);
            REQUIRE_FALSE(s.sharedMarked());
        }
    }
}

TEST_CASE("the extensions folder is known by its text in any spelling", "[addons][addonfolder]")
{
    AddonScratch s("addonfolder-spelled");
    writeAddon(s.extensions);
    const std::vector<fs::path> spellings{upperSpelling(s.extensions), s.user.userRoot / "config" / ".." / "extensions",
                                          fs::path(s.extensions.generic_wstring()),
                                          fs::path(s.extensions).make_preferred()};
    int number = 0;
    for (const fs::path& spelled : spellings) {
        INFO(pathToUtf8(spelled));
        const AddonFolderCopy copy = copyTestAddon(s, s.user.tempFolder / ("job" + std::to_string(++number)), spelled);
        requireOk(copy.status);
        REQUIRE(copy.copied == Names{kAddon});
    }
}

TEST_CASE("no other Blender or Plynth folder is a source", "[addons][addonfolder][safety]")
{
    for (const char* place : {"user/config", "user/config/inner", "user/scripts", "user/scripts/addons", "user/datafiles",
                              "xdg/blenderkit_data", "xdg/blenderkit_data/inner", "user", "appdata/Blender Foundation",
                              "appdata/Blender Foundation/Blender", "appdata/Blender Foundation/Blender/5.2",
                              "appdata/Blender Foundation/Blender/5.2/extensions",
                              "appdata/Blender Foundation/Blender/5.1/extensions", "resources/extensions",
                              "other/portable/extensions", "blender/portable/extensions", "private/extensions"}) {
        DYNAMIC_SECTION(place)
        {
            AddonScratch s("addonfolder-other");
            requireCopyWorks(s);
            writeAddon(s.root / place);
            requireFakeUser(s);
            REQUIRE(spelling(blenderUserFolders(s.install).extensions) == spelling(s.extensions));
            requireRefused(s, s.root / place, s.root / place);
        }
    }
}

TEST_CASE("a source inside or around extensions, empty or relative is refused", "[addons][addonfolder][safety]")
{
    AddonScratch s("addonfolder-inside");
    requireCopyWorks(s);
    const fs::path repository = s.extensions / kRepository;

    SECTION("a repository") { requireRefused(s, repository, repository); }
    SECTION("an add-on") { requireRefused(s, repository / kAddon, repository / kAddon); }
    SECTION("the folder above") { requireRefused(s, s.user.userRoot, s.extensions); }
    SECTION("two folders above") { requireRefused(s, s.root, s.extensions); }
    SECTION("a folder inside that does not exist") { requireRefused(s, s.extensions / "missing", s.extensions); }
    SECTION("empty") { requireRefused(s, {}, s.extensions); }
    SECTION("relative")
    {
        const CurrentFolder here(s.user.userRoot);
        requireRefused(s, "extensions", s.extensions);
    }
    SECTION("the current folder")
    {
        const CurrentFolder here(s.extensions);
        requireRefused(s, ".", repository);
    }
    SECTION("relative to the drive")
    {
        const CurrentFolder here(s.user.userRoot);
        requireRefused(s, fs::path(s.root.root_name().wstring() + L"extensions"), s.extensions);
    }
    SECTION("a trailing dot") { requireRefused(s, fs::path(s.extensions.wstring() + L"."), s.extensions); }
    SECTION("a trailing space") { requireRefused(s, fs::path(s.extensions.wstring() + L" "), s.extensions); }
    SECTION("the long path prefix")
    {
        requireRefused(s, fs::path(L"\\\\?\\" + fs::path(s.extensions).make_preferred().wstring()), s.extensions);
    }
    SECTION("a short name")
    {
        // Only where the volume keeps short names.
        if (const fs::path shorter = shortSpelling(s.extensions); !shorter.empty()) requireRefused(s, shorter, s.extensions);
    }
}

TEST_CASE("an empty source is refused also when no extensions folder is known", "[addons][addonfolder][safety]")
{
    AddonScratch s("addonfolder-unknown");
    requireCopyWorks(s);
    EnvList cleared;
    for (const char* name : {"APPDATA", "BLENDER_USER_RESOURCES", "BLENDER_USER_CONFIG", "BLENDER_USER_SCRIPTS",
                             "BLENDER_USER_EXTENSIONS"}) {
        cleared.push_back(std::make_unique<ScopedEnv>(name, ""));
    }
    REQUIRE(blenderUserFolders(s.install).extensions.empty());
    const fs::path job = s.user.tempFolder / "unknown";
    const AddonFolderCopy copy = prepareAddonFolder(job, {}, s.install, s.cache({moduleOf(kAddon)}), {kAddon});
    REQUIRE(copy.status.code == ErrorCode::InvalidValue);
    REQUIRE_FALSE(fs::exists(job));
}

TEST_CASE("a source reached through a link or junction is refused", "[addons][addonfolder][safety]")
{
    AddonScratch s("addonfolder-linked");
    requireCopyWorks(s);

    SECTION("a junction to extensions as the source")
    {
        const DirectoryLink link(s.root / "work" / "linked", s.extensions);
        requireRefused(s, s.root / "work" / "linked", s.extensions);
    }
    SECTION("extensions itself is a junction")
    {
        const fs::path target = s.root / "other" / "extensions";
        writeAddon(target);
        const fs::path linked = s.root / "work" / "extensions";
        const DirectoryLink link(linked, target);
        const ScopedEnv variable("BLENDER_USER_EXTENSIONS", pathToUtf8(linked));
        requireRefused(s, linked, target);
    }
    SECTION("a folder above extensions is a junction")
    {
        const fs::path target = s.root / "other" / "holder";
        writeAddon(target / "extensions");
        const fs::path linked = s.root / "work" / "holder";
        const DirectoryLink link(linked, target);
        const ScopedEnv variable("BLENDER_USER_EXTENSIONS", pathToUtf8(linked / "extensions"));
        requireRefused(s, linked / "extensions", target);
    }
    SECTION("the AppData Blender folder is a junction")
    {
        EnvList cleared;
        for (const char* name : {"BLENDER_USER_RESOURCES", "BLENDER_USER_CONFIG", "BLENDER_USER_SCRIPTS", "BLENDER_USER_EXTENSIONS"}) {
            cleared.push_back(std::make_unique<ScopedEnv>(name, ""));
        }
        const fs::path target = s.root / "other" / "foundation";
        writeAddon(target / "Blender" / "5.2" / "extensions");
        const fs::path foundation = s.root / "appdata" / "Blender Foundation";
        const DirectoryLink link(foundation, target);
        requireRefused(s, foundation / "Blender" / "5.2" / "extensions", target);
    }
}

TEST_CASE("extensions that share a folder with settings or data are refused", "[addons][addonfolder][safety]")
{
    struct Shared {
        const char* name;
        const char* extensions;
        const char* config;
        const char* scripts;
        const char* xdg;
    };
    for (const Shared& shared :
         {Shared{"every variable names one folder", "other/all", "other/all", "other/all", "xdg"},
          Shared{"is config", "user/config", "user/config", "user/scripts", "xdg"},
          Shared{"lies in config", "user/config/extensions", "user/config", "user/scripts", "xdg"},
          Shared{"holds config", "other/ext", "other/ext/config", "user/scripts", "xdg"},
          Shared{"is scripts", "user/scripts", "user/config", "user/scripts", "xdg"},
          Shared{"lies in scripts", "user/scripts/extensions", "user/config", "user/scripts", "xdg"},
          Shared{"holds scripts", "other/ext", "user/config", "other/ext/scripts", "xdg"},
          Shared{"is the BlenderKit data", "xdg/blenderkit_data", "user/config", "user/scripts", "xdg"},
          Shared{"lies in the BlenderKit data", "xdg/blenderkit_data/ext", "user/config", "user/scripts", "xdg"},
          Shared{"holds the BlenderKit data", "other/ext", "user/config", "user/scripts", "other/ext/xdg"}}) {
        DYNAMIC_SECTION(shared.name)
        {
            AddonScratch s("addonfolder-shared");
            requireCopyWorks(s);
            for (const char* folder : {shared.extensions, shared.config, shared.scripts, shared.xdg}) fs::create_directories(s.root / folder);
            const ScopedEnv extensions("BLENDER_USER_EXTENSIONS", pathToUtf8(s.root / shared.extensions));
            const ScopedEnv config("BLENDER_USER_CONFIG", pathToUtf8(s.root / shared.config));
            const ScopedEnv scripts("BLENDER_USER_SCRIPTS", pathToUtf8(s.root / shared.scripts));
            const ScopedEnv xdg("XDG_DATA_HOME", pathToUtf8(s.root / shared.xdg));
            writeAddon(s.root / shared.extensions);
            requireFakeUser(s);
            const fs::path source = blenderUserFolders(s.install).extensions;
            REQUIRE(spelling(source) == spelling(s.root / shared.extensions));
            requireRefused(s, source, source);
        }
    }

    for (const bool inner : {false, true}) {
        DYNAMIC_SECTION("config resolves into extensions " << inner)
        {
            AddonScratch s("addonfolder-resolved");
            requireCopyWorks(s);
            const fs::path linked = s.root / "work" / "config";
            const DirectoryLink link(linked, inner ? s.extensions / kRepository : s.extensions);
            const ScopedEnv config("BLENDER_USER_CONFIG", pathToUtf8(linked));
            requireRefused(s, s.extensions, s.extensions);
        }
    }
}

TEST_CASE("a cache of another Blender copies nothing and reads nothing", "[addons][addonfolder][safety]")
{
    AddonScratch s("addonfolder-cache");
    requireCopyWorks(s);
    AddonCache cache = s.cache({moduleOf(kAddon)});

    SECTION("another path") { cache.blender = s.root / "other" / "blender.exe"; }
    SECTION("another version") { cache.blenderVersion = "5.3.0"; }
    SECTION("another patch") { cache.blenderVersion = "5.2.1"; }
    SECTION("no Blender named") { cache.blender.clear(); }
    requireRefused(s, s.extensions, s.extensions, cache);
}

TEST_CASE("only the enabled add-ons asked for are opened", "[addons][addonfolder][safety]")
{
    AddonScratch s("addonfolder-enabled");
    writeAddon(s.extensions);
    const fs::path off = writeAddon(s.extensions, "plynth_off");
    const fs::path extra = writeAddon(s.extensions, "plynth_extra");
    const fs::path second = writeAddon(s.extensions, kAddon, "user_default");
    const fs::path legacy = writeAddon(s.root / "user" / "scripts", kAddon, "addons");
    const AddonCache cache =
        s.cache({kAddon, moduleOf(kAddon), moduleOf("plynth_extra"), moduleOf(kAddon, "user_default")});
    const Names before = s.state();

    HeldList held;
    for (const fs::path& folder : {off, extra, second, legacy}) holdAll(held, folder);
    const fs::path job = s.user.tempFolder / "job";
    const AddonFolderCopy copy = copyAddons(s, job, s.extensions, cache, {kAddon, "plynth_off", "plynth_none", kAddon});
    held.clear();

    requireOk(copy.status);
    REQUIRE(copy.copied == Names{kAddon});
    REQUIRE(namesIn(job / "user_default") == Names{kAddon});
    REQUIRE(readTextFile(job / "user_default" / kAddon / "notes.txt") == "from blender_org\n");
    REQUIRE(s.state() == before);
}

TEST_CASE("only the first module of a short name is used", "[addons][addonfolder]")
{
    AddonScratch s("addonfolder-first");
    const fs::path second = writeAddon(s.extensions, kAddon, "user_default");
    writeAddon(s.extensions, "plynth_extra");
    const AddonCache cache = s.cache({moduleOf(kAddon), moduleOf(kAddon, "user_default"), moduleOf("plynth_extra")});
    HeldList held;
    holdAll(held, second);
    const AddonFolderCopy copy = copyAddons(s, s.user.tempFolder / "job", s.extensions, cache, {kAddon, "plynth_extra"});
    held.clear();

    requireOk(copy.status);
    REQUIRE(copy.copied == Names{"plynth_extra"});
}

TEST_CASE("neither extensions nor a repository is ever listed", "[addons][addonfolder][safety]")
{
    AddonScratch s("addonfolder-listing");
    const fs::path addon = writeAddon(s.extensions);
    const fs::path unlisted = writeAddon(s.extensions, "plynth_unlisted");
    const fs::path otherRepository = writeAddon(s.extensions, "plynth_far", "user_default").parent_path();
    writeText(s.extensions / kRepository / "index.json", "{}");
    writeText(addon / "__pycache__" / "module.pyc", "cached");
    const DirectoryLink beside(s.extensions / kRepository / "linked", s.user.userRoot / "config");
    const Names before = s.state();

    HeldList held;
    holdAll(held, unlisted);
    holdAll(held, otherRepository);
    for (const fs::path& path : {s.extensions, s.extensions / kRepository, s.extensions / kRepository / "index.json",
                                 s.user.userRoot / "config", addon / "__pycache__" / "module.pyc"}) {
        held.push_back(std::make_unique<HeldOpen>(path));
    }
    const fs::path job = s.user.tempFolder / "job";
    const AddonFolderCopy copy = copyTestAddon(s, job, s.extensions);
    held.clear();

    requireOk(copy.status);
    REQUIRE(copy.copied == Names{kAddon});
    REQUIRE(namesIn(job) == Names{kMarker, "user_default"});
    REQUIRE(namesIn(job / "user_default") == Names{kAddon});
    REQUIRE_FALSE(fs::exists(job / "user_default" / kAddon / "__pycache__"));
    REQUIRE(s.state() == before);
}

TEST_CASE("an add-on behind or holding a link is left out whole", "[addons][addonfolder][safety]")
{
    AddonScratch s("addonfolder-addon-link");
    writeAddon(s.extensions, "plynth_good", "user_default");
    const fs::path target = s.root / "other" / "target";
    fs::path held = target;
    std::vector<std::unique_ptr<DirectoryLink>> links;
    const fs::path addon = s.extensions / kRepository / kAddon;

    SECTION("the repository is a junction")
    {
        writeAddon(target);
        links.push_back(std::make_unique<DirectoryLink>(s.extensions / kRepository, target / kRepository));
    }
    SECTION("the add-on folder is a junction")
    {
        writeAddon(target);
        fs::create_directories(s.extensions / kRepository);
        links.push_back(std::make_unique<DirectoryLink>(addon, target / kRepository / kAddon));
    }
    SECTION("a junction inside")
    {
        writeAddon(s.extensions);
        writeText(target / "secret.txt", "not to be copied");
        links.push_back(std::make_unique<DirectoryLink>(addon / "data" / "linked", target));
    }
    SECTION("a junction inside to config")
    {
        writeAddon(s.extensions);
        held = s.user.userRoot / "config";
        writeText(held / "userpref.blend", "made-up settings");
        links.push_back(std::make_unique<DirectoryLink>(addon / "linked", held));
    }
    SECTION("a junction in the cache folder")
    {
        writeAddon(s.extensions);
        fs::create_directories(target);
        links.push_back(std::make_unique<DirectoryLink>(addon / "__pycache__" / "linked", target));
    }
    SECTION("a junction to nothing")
    {
        writeAddon(s.extensions);
        links.push_back(std::make_unique<DirectoryLink>(addon / "linked", target));
    }
    SECTION("a junction to the add-on itself")
    {
        writeAddon(s.extensions);
        links.push_back(std::make_unique<DirectoryLink>(addon / "again", addon));
    }
    SECTION("a file link")
    {
        writeAddon(s.extensions);
        writeText(target / "secret.txt", "not to be copied");
        std::error_code ec;
        fs::create_symlink(target / "secret.txt", addon / "linked.txt", ec);
        // Windows makes file links only with a privilege, so a junction stands in without it.
        if (ec) links.push_back(std::make_unique<DirectoryLink>(addon / "linked", target));
    }

    const Names before = s.state();
    HeldList holding;
    if (fs::is_directory(held)) holdAll(holding, held);
    const fs::path job = s.user.tempFolder / "job";
    const AddonFolderCopy copy = copyAddons(s, job, s.extensions,
                                            s.cache({moduleOf(kAddon), moduleOf("plynth_good", "user_default")}),
                                            {kAddon, "plynth_good"});
    holding.clear();

    requireOk(copy.status);
    REQUIRE(copy.copied == Names{"plynth_good"});
    REQUIRE(namesIn(job / "user_default") == Names{"plynth_good"});
    REQUIRE(s.state() == before);
}

TEST_CASE("a module that is not an extension of one repository is left out", "[addons][addonfolder][safety]")
{
    for (const char* module : {"plynth_test_addon", "other.blender_org.plynth_test_addon", "bl_ext.plynth_test_addon",
                               "bl_ext..plynth_test_addon", "bl_ext...plynth_test_addon",
                               "bl_ext.blender_org.sub.plynth_test_addon", "bl_ext.blender-org.plynth_test_addon",
                               "bl_ext.blender_org .plynth_test_addon", "bl_ext.blender_org/sub.plynth_test_addon",
                               "bl_ext.blender_org\\sub.plynth_test_addon", "bl_ext.blender_org.plynth_test_addon/",
                               "bl_ext.blender_org.sub\\plynth_test_addon", "bl_ext.blender_org.", "bl_ext.config_path"}) {
        DYNAMIC_SECTION(module)
        {
            AddonScratch s("addonfolder-module");
            writeAddon(s.extensions);
            writeAddon(s.extensions, kAddon, ".");
            writeAddon(s.extensions, kAddon, "blender-org");
            writeAddon(s.extensions, kAddon, "blender_org/sub");
            writeAddon(s.user.userRoot, kAddon, ".");
            writeAddon(s.user.userRoot / "config", kAddon, ".");
            std::string named = module;
            if (named == "bl_ext.config_path") named = moduleOf(kAddon, pathToUtf8(s.user.userRoot / "config"));
            const fs::path job = s.user.tempFolder / "job";
            const AddonFolderCopy copy = copyAddons(s, job, s.extensions, s.cache({named}), {addonShortName(named)});

            requireOk(copy.status);
            REQUIRE(copy.copied.empty());
            REQUIRE(namesIn(job / "user_default").empty());
        }
    }
}

TEST_CASE("an add-on without a manifest is left out", "[addons][addonfolder]")
{
    AddonScratch s("addonfolder-manifest");
    writeAddon(s.extensions, "plynth_good");
    const fs::path addon = writeAddon(s.extensions);
    fs::remove(addon / "blender_manifest.toml");

    SECTION("no manifest") {}
    SECTION("the manifest is a folder") { fs::create_directories(addon / "blender_manifest.toml"); }
    SECTION("no folder") { fs::remove_all(addon); }

    const fs::path job = s.user.tempFolder / "job";
    const AddonFolderCopy copy =
        copyAddons(s, job, s.extensions, s.cache({moduleOf(kAddon), moduleOf("plynth_good")}), {kAddon, "plynth_good"});
    requireOk(copy.status);
    REQUIRE(copy.copied == Names{"plynth_good"});
    REQUIRE(fs::is_regular_file(job / kMarker));
}

TEST_CASE("a copy keeps names and bytes and only reads the source", "[addons][addonfolder]")
{
    AddonScratch s("addonfolder-content");
    const fs::path first = writeAddon(s.extensions);
    const fs::path mixed = writeAddon(s.extensions, kMixed, "user_default");
    const std::string bytes("\x00\x01\r\n\xff", 5);
    writeText(mixed / "Data" / "Deep" / "table.bin", bytes);
    writeText(mixed / pathFromUtf8("caf\xc3\xa9.txt"), "name");
    writeText(mixed / ".settings", "dot");
    writeText(mixed / "Data" / "__pycache__" / "a.pyc", "cached");
    writeText(first / "__pycache__" / "b.pyc", "cached");
    const Names before = s.state();

    HeldList held;
    for (const fs::path& file : {first / "blender_manifest.toml", mixed / "Data" / "Deep" / "table.bin"}) {
        held.push_back(std::make_unique<HeldOpen>(file, FILE_SHARE_READ));
    }
    const fs::path job = s.user.tempFolder / "job";
    const AddonFolderCopy copy = copyAddons(s, job, s.extensions,
                                            s.cache({moduleOf(kAddon), moduleOf(kMixed, "user_default")}),
                                            {kAddon, "plynth_mixed"});
    held.clear();

    requireOk(copy.status);
    REQUIRE(copy.copied == Names{"plynth_mixed", kAddon});
    const fs::path made = job / "user_default";
    REQUIRE(namesIn(made) == Names{kMixed, kAddon});
    REQUIRE(namesIn(made / kMixed / "Data") == Names{"Deep"});
    REQUIRE(readTextFile(made / kMixed / "Data" / "Deep" / "table.bin") == bytes);
    REQUIRE(readTextFile(made / kMixed / pathFromUtf8("caf\xc3\xa9.txt")) == "name");
    REQUIRE(readTextFile(made / kMixed / ".settings") == "dot");
    REQUIRE(readTextFile(made / kAddon / "__init__.py") == readTextFile(first / "__init__.py"));
    REQUIRE_FALSE(fs::exists(made / kAddon / "__pycache__"));
    REQUIRE(fs::is_regular_file(job / kMarker));
    REQUIRE(s.state() == before);
    REQUIRE_FALSE(s.sharedMarked());
}

TEST_CASE("read-only files are copied without the attribute", "[addons][addonfolder]")
{
    AddonScratch s("addonfolder-readonly");
    const fs::path addon = writeAddon(s.extensions);
    writeText(addon / "data" / "locked.txt", "read only");
    const std::vector<fs::path> files{addon / "blender_manifest.toml", addon / "data" / "locked.txt"};
    for (const fs::path& file : files) REQUIRE(SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY));
    const Names before = s.state();

    const fs::path job = s.user.tempFolder / "job";
    const AddonFolderCopy copy = copyTestAddon(s, job, s.extensions);
    const Names after = s.state();
    for (const fs::path& file : files) {
        const fs::path made = job / "user_default" / kAddon / file.lexically_relative(addon);
        const DWORD attributes = GetFileAttributesW(made.c_str());
        CHECK(attributes != INVALID_FILE_ATTRIBUTES);
        CHECK((attributes & FILE_ATTRIBUTE_READONLY) == 0);
        SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
    }

    requireOk(copy.status);
    REQUIRE(after == before);
    fs::remove_all(job);
    REQUIRE_FALSE(fs::exists(job));
}

TEST_CASE("a copy that fails has no marker", "[addons][addonfolder]")
{
    AddonScratch s("addonfolder-failed");
    requireCopyWorks(s);
    const fs::path other = writeAddon(s.extensions, "plynth_zlast");
    const AddonCache cache = s.cache({moduleOf(kAddon), moduleOf("plynth_zlast")});
    fs::path job = s.user.tempFolder / "job";

    SECTION("a file cannot be read")
    {
        const HeldOpen held(other / "__init__.py");
        const AddonFolderCopy copy = copyAddons(s, job, s.extensions, cache, {kAddon, "plynth_zlast"});
        REQUIRE(copy.status.code == ErrorCode::CannotRead);
        REQUIRE_FALSE(fs::exists(job / kMarker));
    }
    SECTION("the job folder cannot be made")
    {
        writeText(s.user.tempFolder / "file", "in the way");
        job = s.user.tempFolder / "file" / "job";
        const AddonFolderCopy copy = copyAddons(s, job, s.extensions, cache, {kAddon});
        REQUIRE(copy.status.code == ErrorCode::CannotWrite);
        REQUIRE_FALSE(fs::exists(job / kMarker));
    }
    SECTION("stopped before it starts")
    {
        std::stop_source stop;
        stop.request_stop();
        const AddonFolderCopy copy = copyAddons(s, job, s.extensions, cache, {kAddon}, stop.get_token());
        REQUIRE(copy.status.code == ErrorCode::Cancelled);
        REQUIRE_FALSE(fs::exists(job / kMarker));
    }
    REQUIRE_FALSE(s.sharedMarked());
}

TEST_CASE("the marker comes last and never after a stop", "[addons][addonfolder]")
{
    AddonScratch s("addonfolder-marker");
    const fs::path addon = writeAddon(s.extensions);
    constexpr int kFiles = 500;
    for (int i = 0; i < kFiles; ++i) writeText(addon / "many" / (std::to_string(i) + ".txt"), "x");
    const fs::path job = s.user.tempFolder / "job";
    const fs::path many = job / "user_default" / kAddon / "many";
    std::atomic<bool> done{false};
    requireFakeUser(s);

    SECTION("written last")
    {
        std::atomic<std::ptrdiff_t> atMarker{-1};
        std::thread watcher([&] {
            while (!done) {
                std::error_code ec;
                if (!fs::exists(job / kMarker, ec)) {
                    std::this_thread::yield();
                    continue;
                }
                std::ptrdiff_t count = 0;
                for (fs::directory_iterator it(many, ec), end; !ec && it != end; it.increment(ec)) ++count;
                atMarker = count;
                return;
            }
        });
        const AddonFolderCopy copy = prepareAddonFolder(job, s.extensions, s.install, s.cache({moduleOf(kAddon)}), {kAddon});
        done = true;
        watcher.join();
        requireOk(copy.status);
        REQUIRE((atMarker == -1 || atMarker == kFiles));
        REQUIRE(fs::is_regular_file(job / kMarker));
    }
    SECTION("stopped between files")
    {
        std::stop_source stop;
        std::thread watcher([&] {
            while (!done) {
                std::error_code ec;
                if (fs::exists(many, ec)) {
                    stop.request_stop();
                    return;
                }
                std::this_thread::yield();
            }
        });
        const AddonFolderCopy copy =
            prepareAddonFolder(job, s.extensions, s.install, s.cache({moduleOf(kAddon)}), {kAddon}, stop.get_token());
        done = true;
        watcher.join();
        REQUIRE(copy.status.code == ErrorCode::Cancelled);
        REQUIRE_FALSE(fs::exists(job / kMarker));
    }
}

TEST_CASE("a job folder that exists is refused and left as it was", "[addons][addonfolder]")
{
    AddonScratch s("addonfolder-job-exists");
    requireCopyWorks(s);
    const fs::path job = s.root / "work" / "job";
    std::unique_ptr<DirectoryLink> link;

    SECTION("empty") { fs::create_directories(job); }
    SECTION("filled") { writeText(job / "user_default" / "keep.txt", "keep"); }
    SECTION("marked") { writeText(job / kMarker, ""); }
    SECTION("a file") { writeText(job, "a file"); }
    SECTION("a junction")
    {
        fs::create_directories(s.root / "work" / "target");
        link = std::make_unique<DirectoryLink>(job, s.root / "work" / "target");
    }
    const Names before = treeState(s.root / "work");
    REQUIRE(copyTestAddon(s, job, s.extensions).status.code == ErrorCode::InvalidValue);
    REQUIRE(treeState(s.root / "work") == before);
}

TEST_CASE("a job folder in or around a user folder is refused before it is made", "[addons][addonfolder][safety]")
{
    AddonScratch s("addonfolder-job-place");
    requireCopyWorks(s);
    fs::create_directories(s.root / "appdata" / "Blender Foundation");
    std::unique_ptr<DirectoryLink> link;
    std::unique_ptr<ScopedEnv> variable;
    fs::path job;

    SECTION("in the source") { job = s.extensions / "job"; }
    SECTION("in a repository") { job = s.extensions / kRepository / "job"; }
    SECTION("in the source in capitals") { job = upperSpelling(s.extensions) / "job"; }
    SECTION("in the source through dots") { job = s.root / "work" / ".." / "user" / "extensions" / "job"; }
    SECTION("in the source with a trailing dot") { job = fs::path(s.extensions.wstring() + L".") / "job"; }
    SECTION("in config") { job = s.user.userRoot / "config" / "job"; }
    SECTION("in scripts") { job = s.user.userRoot / "scripts" / "job"; }
    SECTION("in the BlenderKit data") { job = s.root / "xdg" / "blenderkit_data" / "job"; }
    SECTION("in the AppData Blender folder") { job = s.root / "appdata" / "Blender Foundation" / "job"; }
    SECTION("around the BlenderKit data")
    {
        job = s.root / "other" / "holder";
        variable = std::make_unique<ScopedEnv>("XDG_DATA_HOME", pathToUtf8(job));
    }
    SECTION("around the AppData Blender folder")
    {
        job = s.root / "other" / "holder";
        variable = std::make_unique<ScopedEnv>("APPDATA", pathToUtf8(job));
    }
    SECTION("in the source through a junction")
    {
        link = std::make_unique<DirectoryLink>(s.root / "work" / "linked", s.extensions);
        job = s.root / "work" / "linked" / "job";
    }
    SECTION("in config through a junction")
    {
        link = std::make_unique<DirectoryLink>(s.root / "work" / "linked", s.user.userRoot / "config");
        job = s.root / "work" / "linked" / "job";
    }
    SECTION("relative") { job = "job"; }

    const Names before = s.state();
    const CurrentFolder here(s.root / "work");
    const AddonFolderCopy copy = copyTestAddon(s, job, s.extensions);
    REQUIRE(copy.status.code == ErrorCode::InvalidValue);
    REQUIRE_FALSE(fs::exists(job));
    REQUIRE(s.state() == before);
}

namespace {

// A build job that starts in the work folder of the test.
Json smallBuildJob(const AddonScratch& s)
{
    const Project project = squareProjectWithWalls();
    const fs::path folder = s.root / "work" / "Project";
    fs::create_directories(folder / "build");
    return makeBuildJob(project, makeBuildPlan(project, {}), BuildSettings{}, folder, "build-1a2b3c4d",
                        s.root / "work" / "run" / "result.json");
}

// A folder with the marker and one add-on, as prepareAddonFolder would leave it.
fs::path markedFolder(const fs::path& folder)
{
    writeText(folder / "user_default" / kAddon / "blender_manifest.toml", "id = \"plynth_test_addon\"\n");
    writeText(folder / kMarker, "");
    return folder;
}

// The lines of a log the fake Blender wrote beside its program.
Names fakeLog(const fs::path& exe, const char* name)
{
    Names lines;
    std::ifstream in(exe.parent_path() / name, std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

// How many jobs of a task the fake Blender was given.
int jobCount(const fs::path& exe, const std::string& task)
{
    int count = 0;
    for (const std::string& line : fakeLog(exe, "fake_jobs.log")) count += nlohmann::json::parse(line).at("task") == task ? 1 : 0;
    return count;
}

// What the fake Blender saw in the extensions folder of the one job with this task.
nlohmann::json extensionsOf(const fs::path& exe, const std::string& task)
{
    std::vector<nlohmann::json> found;
    for (const std::string& line : fakeLog(exe, "fake_extensions.log")) {
        const nlohmann::json seen = nlohmann::json::parse(line);
        if (seen.at("task") == task) found.push_back(seen);
    }
    REQUIRE(found.size() == 1);
    return found[0];
}

// One user variable of the last job the fake Blender was started for.
fs::path childVariable(const fs::path& exe, const std::string& name)
{
    fs::path value;
    for (const std::string& line : fakeLog(exe, "fake_env.log")) {
        if (line.rfind("job|", 0) != 0) continue;
        const std::string key = "|" + name + "=";
        const std::size_t at = line.find(key);
        REQUIRE(at != std::string::npos);
        const std::size_t start = at + key.size();
        value = pathFromUtf8(line.substr(start, line.find('|', start) - start));
    }
    return value;
}

// Checks the four user variables that are always the shared private folder.
void requirePrivateFour(const AddonScratch& s)
{
    const fs::path shared = s.root / "private";
    REQUIRE(spelling(childVariable(s.exe, "BLENDER_USER_RESOURCES")) == spelling(shared));
    REQUIRE(spelling(childVariable(s.exe, "BLENDER_USER_CONFIG")) == spelling(shared / "config"));
    REQUIRE(spelling(childVariable(s.exe, "BLENDER_USER_SCRIPTS")) == spelling(shared / "scripts"));
    REQUIRE(spelling(childVariable(s.exe, "BLENDER_USER_DATAFILES")) == spelling(shared / "datafiles"));
}

// Runs a job after checking that only made-up folders can be reached.
BlenderJobRun runFakeJob(const AddonScratch& s, const Json& job, const BlenderRunOptions& options)
{
    requireFakeUser(s);
    return runBlenderJob(s.exe, job, s.root / "work" / "run", options);
}

}

TEST_CASE("a job gets its own extensions and four shared private folders", "[addons][blenderjob][safety]")
{
    AddonScratch s("addonjob-env");
    BlenderRunOptions options;
    options.scriptsFolder = s.scripts;
    const Names before = s.state();

    SECTION("with a folder of its own")
    {
        options.extensionsFolder = markedFolder(s.user.tempFolder / "plynth-ext-0a1b2c3d");
        requireOk(runFakeJob(s, smallBuildJob(s), options).status);
        REQUIRE(fs::equivalent(childVariable(s.exe, "BLENDER_USER_EXTENSIONS"), options.extensionsFolder));
        const nlohmann::json seen = extensionsOf(s.exe, "build");
        REQUIRE(seen.at("marker") == true);
        REQUIRE(seen.at("user_default").get<Names>() == Names{kAddon});
        REQUIRE(fs::exists(options.extensionsFolder / kMarker));
    }
    SECTION("without one")
    {
        requireOk(runFakeJob(s, smallBuildJob(s), options).status);
        REQUIRE(spelling(childVariable(s.exe, "BLENDER_USER_EXTENSIONS")) == spelling(s.root / "private" / "extensions"));
        REQUIRE(extensionsOf(s.exe, "build").at("marker") == false);
    }
    requirePrivateFour(s);
    REQUIRE(spelling(variablePath("BLENDER_USER_EXTENSIONS")) == spelling(s.extensions));
    REQUIRE_FALSE(s.sharedMarked());
    REQUIRE(s.state() == before);
}

TEST_CASE("the probe ignores a folder of its own", "[addons][blenderjob][safety]")
{
    AddonScratch s("addonjob-probe");
    BlenderRunOptions options;
    options.scriptsFolder = s.scripts;

    SECTION("a marked folder") { options.extensionsFolder = markedFolder(s.user.tempFolder / "own"); }
    SECTION("a folder that would be refused") { options.extensionsFolder = "relative"; }
    SECTION("without factory startup")
    {
        options.factoryStartup = false;
        options.extensionsFolder = markedFolder(s.user.tempFolder / "own");
    }
    requireFakeUser(s);
    const Json job = makeProbeJob(blenderUserFolders(s.install), "probe-00000001", s.root / "work" / "run" / "result.json");
    requireOk(runFakeJob(s, job, options).status);
    requirePrivateFour(s);
    REQUIRE(spelling(childVariable(s.exe, "BLENDER_USER_EXTENSIONS")) == spelling(s.root / "private" / "extensions"));
    REQUIRE(extensionsOf(s.exe, "probe").at("marker") == false);
    for (const std::string& line : fakeLog(s.exe, "fake_args.log")) REQUIRE(line.find("--factory-startup") != std::string::npos);
}

TEST_CASE("runBlenderJob refuses a wrong extensions folder and starts nothing", "[addons][blenderjob][safety]")
{
    AddonScratch s("addonjob-refused");
    BlenderRunOptions options;
    options.scriptsFolder = s.scripts;
    options.extensionsFolder = markedFolder(s.user.tempFolder / "own");
    std::unique_ptr<DirectoryLink> link;
    const fs::path foundation = s.root / "appdata" / "Blender Foundation";
    const fs::path shared = s.root / "private";

    SECTION("without factory startup") { options.factoryStartup = false; }
    SECTION("relative")
    {
        markedFolder(s.root / "work" / "relative");
        options.extensionsFolder = "relative";
    }
    SECTION("no marker") { fs::remove(options.extensionsFolder / kMarker); }
    SECTION("the marker is a folder")
    {
        fs::remove(options.extensionsFolder / kMarker);
        fs::create_directories(options.extensionsFolder / kMarker);
    }
    SECTION("not there") { options.extensionsFolder = s.user.tempFolder / "none"; }
    SECTION("the private folder") { options.extensionsFolder = markedFolder(shared); }
    SECTION("the shared extensions") { options.extensionsFolder = markedFolder(shared / "extensions"); }
    SECTION("inside the private folder") { options.extensionsFolder = markedFolder(shared / "own"); }
    SECTION("the private folder in capitals") { options.extensionsFolder = upperSpelling(markedFolder(shared / "own")); }
    SECTION("the private folder through a junction")
    {
        markedFolder(shared / "own");
        link = std::make_unique<DirectoryLink>(s.root / "work" / "linked", shared);
        options.extensionsFolder = s.root / "work" / "linked" / "own";
    }
    SECTION("the extensions of this process") { options.extensionsFolder = markedFolder(s.extensions); }
    SECTION("inside the extensions of this process") { options.extensionsFolder = markedFolder(s.extensions / "own"); }
    SECTION("the extensions of this process through a junction")
    {
        markedFolder(s.extensions);
        link = std::make_unique<DirectoryLink>(s.root / "work" / "linked", s.extensions);
        options.extensionsFolder = s.root / "work" / "linked";
    }
    SECTION("the AppData Blender folder") { options.extensionsFolder = markedFolder(foundation); }
    SECTION("inside the AppData Blender folder")
    {
        options.extensionsFolder = markedFolder(foundation / "Blender" / "5.2" / "extensions");
    }
    SECTION("the AppData Blender folder through dots")
    {
        markedFolder(foundation / "own");
        options.extensionsFolder = s.root / "appdata" / ".." / "appdata" / "Blender Foundation" / "own";
    }
    SECTION("the AppData Blender folder through a junction")
    {
        markedFolder(foundation / "own");
        link = std::make_unique<DirectoryLink>(s.root / "work" / "linked", foundation);
        options.extensionsFolder = s.root / "work" / "linked" / "own";
    }

    const Json job = smallBuildJob(s);
    const Names before = s.state();
    const CurrentFolder here(s.root / "work");
    const BlenderJobRun run = runFakeJob(s, job, options);
    REQUIRE(run.status.code == ErrorCode::InvalidValue);
    REQUIRE_FALSE(run.status.message.empty());
    REQUIRE(fakeLog(s.exe, "fake_args.log").empty());
    REQUIRE(s.state() == before);
}

namespace {

// A library of made-up generators, a project and a build request for the fake Blender.
struct AddonBuild {
    explicit AddonBuild(const std::string& tag) : s(tag)
    {
        library = libraryPaths(s.root / "Library");
        requireOk(initLibrary(s.root / "Library"));
        generator("plain", {});
        generator("needs_test", {kAddon});
        generator("needs_mixed", {"plynth_mixed"});
        generator("needs_two", {"plynth_spare", "plynth_off"});
        generator("unused", {"plynth_unused"});
        writeAddon(s.extensions);
        writeAddon(s.extensions, kMixed, "user_default");
        for (const char* id : {"plynth_spare", "plynth_off", "plynth_unused"}) writeAddon(s.extensions, id);
        request.projectFolder = s.root / "Project";
        fs::create_directories(request.projectFolder);
        request.project = squareProjectWithWalls();
        clearMaterialDefaults(request.project);
        Room room;
        room.id = "r1";
        room.walls = {"w1", "w2", "w3", "w4"};
        request.project.plan.floors[0].rooms = {room};
        request.library = library;
        request.blender = s.install;
        request.run.scriptsFolder = s.scripts;
        request.addonSource = s.extensions;
    }

    // Writes one generator that needs the given add-ons.
    void generator(const std::string& id, const Names& addons) const
    {
        writeText(library.generators / (id + ".py"),
                  "\"\"\"plynth-generator\n"
                  R"({"format": "plynth-generator", "version": 1, "name": "Test part", "description": "A part for tests.", )"
                  R"("addons": )" + Json(addons).dump() +
                      R"(, "size": {"width": 0.5, "depth": 0.5, "height": 1.0}, )"
                      R"("params": [{"id": "amount", "label": "Amount", "type": "number", "default": 1.0}]})"
                      "\n\"\"\"\n\ndef generate(gen):\n    return []\n");
    }

    // Places one item of a generator.
    void place(const std::string& generator, const std::string& id)
    {
        const GeneratorLibrary generators = loadGeneratorFolder(library.generators);
        const GeneratorInfo* info = generators.find(generator);
        REQUIRE(info != nullptr);
        const GeneratorPlacement placed = makeGeneratorUse(*info, defaultGeneratorParams(*info, 3.0));
        requireOk(placed.status);
        std::vector<Item>& items = request.project.plan.floors[0].items;
        Item item = buildItem(id, "", 0.7 + 0.8 * static_cast<double>(items.size()), 2.0, placed.size);
        item.generator = placed.use;
        items.push_back(item);
    }

    // Saves the add-on cache of this Blender.
    void save(const Names& modules) const
    {
        REQUIRE(insideFolder(addonCacheFile(), s.root));
        requireOk(writeAddonCache(addonCacheFile(), s.cache(modules)));
    }

    // Builds after checking that only made-up folders can be reached.
    BuildReport build(const std::function<void(const BuildProgress&)>& progress = {}, std::stop_token stop = {}) const
    {
        requireFakeUser(s);
        return runBuild(request, progress, stop);
    }

    // The temporary job folders that are there now.
    Names jobFolders() const
    {
        Names found;
        for (const std::string& name : namesIn(s.user.tempFolder)) {
            if (name.rfind("plynth-ext-", 0) == 0) found.push_back(name);
        }
        return found;
    }

    // Switches what the fake Blender does next.
    void setMode(const std::string& mode) const { writeText(s.exe.parent_path() / "fake_mode.txt", mode); }

    AddonScratch s;
    LibraryPaths library;
    BuildRequest request;
};

// The generator_unavailable problems of a report.
std::vector<BuildProblem> unavailableIn(const BuildReport& report)
{
    std::vector<BuildProblem> found;
    for (const BuildProblem& problem : report.problems) {
        if (problem.code == kGeneratorUnavailableProblem) found.push_back(problem);
    }
    return found;
}

// Checks the one warning that runBuild gives when nothing could be copied.
void requireBuildWarning(const BuildReport& report)
{
    const std::vector<BuildProblem> found = unavailableIn(report);
    REQUIRE(found.size() == 1);
    REQUIRE(found[0].severity == BuildSeverity::Warning);
    REQUIRE(found[0].floor.empty());
    REQUIRE(found[0].object.empty());
    REQUIRE_FALSE(found[0].message.empty());
    REQUIRE(report.errorCount() == 0);
}

// The generator items of the one build job the fake Blender was given.
Names itemsInJob(const AddonBuild& f)
{
    Names ids;
    int jobs = 0;
    for (const std::string& line : fakeLog(f.s.exe, "fake_jobs.log")) {
        const nlohmann::json job = nlohmann::json::parse(line);
        if (job.at("task") != "build") continue;
        ++jobs;
        for (const auto& floor : job.at("params").at("floors")) {
            for (const auto& item : floor.at("generator_items")) ids.push_back(item.at("id").get<std::string>());
        }
    }
    REQUIRE(jobs == 1);
    return ids;
}

// Checks that the build job saw no copy and the shared private extensions folder.
void requireNothingCopied(const AddonBuild& f)
{
    const nlohmann::json seen = extensionsOf(f.s.exe, "build");
    REQUIRE(seen.at("marker") == false);
    REQUIRE(seen.at("user_default").empty());
    REQUIRE(spelling(childVariable(f.s.exe, "BLENDER_USER_EXTENSIONS")) == spelling(f.s.root / "private" / "extensions"));
    REQUIRE(f.jobFolders().empty());
    REQUIRE_FALSE(f.s.sharedMarked());
}

// Runs a step once, when the build reaches Blender.
std::function<void(const BuildProgress&)> atBlender(std::function<void()> action)
{
    return [action, reached = std::make_shared<bool>(false)](const BuildProgress& progress) {
        if (progress.stage != BuildStage::Blender || *reached) return;
        *reached = true;
        action();
    };
}

}

TEST_CASE("runBuild copies what a fresh probe allows and always removes the copy", "[addons][build][safety]")
{
    struct Ending {
        const char* name;
        const char* mode;
        ErrorCode code;
        bool stopped;
    };
    for (const Ending& ending :
         {Ending{"ok", "ok", ErrorCode::None, false}, Ending{"fail", "fail", ErrorCode::BlenderFailed, false},
          Ending{"crash", "crash", ErrorCode::BlenderFailed, false}, Ending{"bad result", "bad_result", ErrorCode::BlenderFailed, false},
          Ending{"timeout", "hang", ErrorCode::BlenderFailed, false}, Ending{"cancel", "hang", ErrorCode::Cancelled, true}}) {
        DYNAMIC_SECTION(ending.name)
        {
            AddonBuild f("addonbuild-ending");
            f.place("needs_test", "g1");
            f.place("needs_mixed", "g2");
            f.place("needs_two", "g3");
            f.place("plain", "g4");
            f.save({moduleOf(kAddon)});
            const std::string enabled = moduleOf(kAddon) + "," + moduleOf(kMixed, "user_default") + "," +
                                        moduleOf("plynth_spare") + "," + moduleOf("plynth_unused");
            const ScopedEnv probe("FAKE_BLENDER_ADDONS", enabled);
            // The saved cache says two of the needed add-ons are off, and only the fresh answer counts.
            f.request.run.factoryStartup = false;
            f.request.run.timeout = ending.stopped ? std::chrono::seconds(60) : std::chrono::seconds(5);

            const Names before = f.s.state();
            HeldList held;
            for (const char* id : {"plynth_spare", "plynth_off", "plynth_unused"}) holdAll(held, f.s.extensions / kRepository / id);

            std::stop_source stop;
            std::atomic<bool> over{false};
            std::thread stopper;
            Names during;
            const auto progress = atBlender([&] {
                during = f.jobFolders();
                f.setMode(ending.mode);
                if (!ending.stopped) return;
                stopper = std::thread([&] {
                    for (int i = 0; i < 600 && !over; ++i) {
                        if (fakeLog(f.s.exe, "fake_extensions.log").size() >= 2) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                    stop.request_stop();
                });
            });
            const BuildReport report = f.build(progress, stop.get_token());
            over = true;
            if (stopper.joinable()) stopper.join();
            held.clear();

            REQUIRE(report.status.code == ending.code);
            REQUIRE(jobCount(f.s.exe, "probe") == 1);
            const nlohmann::json seen = extensionsOf(f.s.exe, "build");
            REQUIRE(seen.at("marker") == true);
            REQUIRE(seen.at("user_default").get<Names>() == Names{kMixed, kAddon});
            REQUIRE(itemsInJob(f) == Names{"g1", "g2", "g4"});

            const fs::path own = childVariable(f.s.exe, "BLENDER_USER_EXTENSIONS");
            REQUIRE(std::regex_match(pathToUtf8(own.filename()), std::regex("plynth-ext-[0-9a-f]{8}")));
            REQUIRE(fs::equivalent(own.parent_path(), f.s.user.tempFolder));
            requirePrivateFour(f.s);
            REQUIRE_FALSE(fs::exists(own));
            REQUIRE(f.jobFolders().empty());
            REQUIRE(during.size() <= 1);
            for (const std::string& line : fakeLog(f.s.exe, "fake_args.log")) REQUIRE(line.find("--factory-startup") != std::string::npos);
            REQUIRE(f.s.state() == before);
            REQUIRE_FALSE(f.s.sharedMarked());

            const std::vector<BuildProblem> left = unavailableIn(report);
            REQUIRE(left.size() == 1);
            REQUIRE(left[0].object == "g3");
            if (report.status.ok()) {
                REQUIRE(report.counts.generators == 3);
                const nlohmann::json written = nlohmann::json::parse(readTextFile(f.request.projectFolder / "build" / "build.json"));
                REQUIRE(written.at("counts").at("generators") == 3);
                REQUIRE(schemaProblem("build-report.schema.json", written).empty());
            }
        }
    }
}

TEST_CASE("the copy is removed when the build throws", "[addons][build][safety]")
{
    for (const BuildStage stage : {BuildStage::Blender, BuildStage::Finishing}) {
        DYNAMIC_SECTION(buildStageName(stage))
        {
            AddonBuild f("addonbuild-throws");
            f.place("needs_test", "g1");
            const ScopedEnv probe("FAKE_BLENDER_ADDONS", moduleOf(kAddon));
            const auto progress = [stage](const BuildProgress& now) {
                if (now.stage == stage) throw std::runtime_error("stop here");
            };
            REQUIRE_THROWS_AS(f.build(progress), std::runtime_error);
            REQUIRE(jobCount(f.s.exe, "probe") == 1);
            REQUIRE(f.jobFolders().empty());
            REQUIRE_FALSE(fs::exists(f.request.projectFolder / "build" / "build.lock"));
        }
    }
}

TEST_CASE("every build that copies asks Blender again", "[addons][build]")
{
    AddonBuild f("addonbuild-again");
    f.place("needs_test", "g1");
    const ScopedEnv probe("FAKE_BLENDER_ADDONS", moduleOf(kAddon));

    SECTION("no cache") {}
    SECTION("a cache of this Blender") { f.save({moduleOf(kAddon)}); }
    SECTION("a cache of another Blender that says off")
    {
        AddonCache other = f.s.cache({});
        other.blender = f.s.root / "other" / "blender.exe";
        requireOk(writeAddonCache(addonCacheFile(), other));
    }
    SECTION("a cache of another version that says off")
    {
        AddonCache other = f.s.cache({});
        other.blenderVersion = "5.1.0";
        requireOk(writeAddonCache(addonCacheFile(), other));
    }
    SECTION("a cache that cannot be read") { writeText(addonCacheFile(), "{ not json"); }

    requireOk(f.build().status);
    REQUIRE(jobCount(f.s.exe, "probe") == 1);
    const fs::path first = childVariable(f.s.exe, "BLENDER_USER_EXTENSIONS");
    REQUIRE(extensionsOf(f.s.exe, "build").at("marker") == true);
    requireOk(f.build().status);
    REQUIRE(jobCount(f.s.exe, "probe") == 2);
    REQUIRE(childVariable(f.s.exe, "BLENDER_USER_EXTENSIONS") != first);
    REQUIRE(f.jobFolders().empty());
}

TEST_CASE("an add-on switched off since the last check is not copied", "[addons][build][safety]")
{
    AddonBuild f("addonbuild-stale");
    f.place("needs_test", "g1");
    f.place("plain", "g2");
    f.save({moduleOf(kAddon)});
    const ScopedEnv probe("FAKE_BLENDER_ADDONS", "");
    const Names before = f.s.state();

    HeldList held;
    holdAll(held, f.s.extensions / kRepository / kAddon);
    Names during;
    int probesBefore = -1;
    const BuildReport report = f.build(atBlender([&] {
        during = f.jobFolders();
        probesBefore = jobCount(f.s.exe, "probe");
    }));
    held.clear();

    requireOk(report.status);
    REQUIRE(probesBefore == 1);
    REQUIRE(jobCount(f.s.exe, "probe") == 1);
    REQUIRE(during.empty());
    requireNothingCopied(f);
    const std::vector<BuildProblem> left = unavailableIn(report);
    REQUIRE(left.size() == 1);
    REQUIRE(left[0].object == "g1");
    REQUIRE(left[0].severity == BuildSeverity::Warning);
    REQUIRE(left[0].step == kCheckStep);
    REQUIRE(report.errorCount() == 0);
    REQUIRE(itemsInJob(f) == Names{"g2"});
    REQUIRE(report.counts.generators == 1);

    const AddonCacheRead saved = readAddonCache(addonCacheFile());
    requireOk(saved.status);
    REQUIRE(saved.cache.has_value());
    REQUIRE_FALSE(addonEnabled(*saved.cache, kAddon));
    REQUIRE(f.s.state() == before);
}

TEST_CASE("a build that would copy nothing runs no probe", "[addons][build][safety]")
{
    AddonBuild f("addonbuild-nothing");
    const ScopedEnv probe("FAKE_BLENDER_ADDONS", moduleOf(kAddon));
    bool warned = false;
    Names built;

    SECTION("only plain generators")
    {
        f.place("plain", "g1");
        built = {"g1"};
    }
    SECTION("no items") {}
    SECTION("the saved cache says off")
    {
        f.place("needs_test", "g1");
        f.save({});
    }
    SECTION("the saved cache says one of two is off")
    {
        f.place("needs_two", "g1");
        f.save({moduleOf("plynth_spare")});
    }
    SECTION("the generator is gone")
    {
        f.place("needs_test", "g1");
        fs::remove(f.library.generators / "needs_test.py");
    }
    SECTION("no library")
    {
        f.place("needs_test", "g1");
        f.request.library.reset();
    }
    SECTION("no source")
    {
        f.place("needs_test", "g1");
        f.request.addonSource.clear();
        built = {"g1"};
    }
    SECTION("another folder as the source")
    {
        f.place("needs_test", "g1");
        f.request.addonSource = f.s.root / "work" / "beside";
        writeAddon(f.request.addonSource);
        warned = true;
        built = {"g1"};
    }

    const Names before = f.s.state();
    HeldList held;
    holdAll(held, f.s.extensions);
    const BuildReport report = f.build();
    held.clear();

    requireOk(report.status);
    REQUIRE(jobCount(f.s.exe, "probe") == 0);
    REQUIRE(itemsInJob(f) == built);
    REQUIRE(report.counts.generators == static_cast<int>(built.size()));
    requireNothingCopied(f);
    if (warned) requireBuildWarning(report);
    REQUIRE(f.s.state() == before);
}

TEST_CASE("without a source runBuild never fills one in", "[addons][build][safety]")
{
    AddonBuild f("addonbuild-default");
    f.place("needs_test", "g1");
    REQUIRE(BuildRequest{}.addonSource.empty());
    f.request.addonSource.clear();
    const ScopedEnv probe("FAKE_BLENDER_ADDONS", moduleOf(kAddon));
    const ScopedEnv copies("PLYNTH_TEST_ADDONS", pathToUtf8(f.s.extensions));

    SECTION("no cache") {}
    SECTION("a matching cache") { f.save({moduleOf(kAddon)}); }
    SECTION("a request with normal startup")
    {
        f.save({moduleOf(kAddon)});
        f.request.run.factoryStartup = false;
    }
    SECTION("a request with an extensions folder")
    {
        f.save({moduleOf(kAddon)});
        f.request.run.extensionsFolder = markedFolder(f.s.root / "work" / "own");
    }

    const Names before = f.s.state();
    HeldList held;
    holdAll(held, f.s.extensions);
    const BuildReport report = f.build();
    held.clear();

    requireOk(report.status);
    REQUIRE(jobCount(f.s.exe, "probe") == 0);
    requireNothingCopied(f);
    requirePrivateFour(f.s);
    REQUIRE(unavailableIn(report).empty());
    REQUIRE(itemsInJob(f) == Names{"g1"});
    REQUIRE(report.counts.generators == 1);
    for (const std::string& line : fakeLog(f.s.exe, "fake_args.log")) REQUIRE(line.find("--factory-startup") != std::string::npos);
    REQUIRE(f.s.state() == before);
}

TEST_CASE("a refused source warns once and the build goes on", "[addons][build][safety]")
{
    AddonBuild f("addonbuild-refused");
    f.place("needs_test", "g1");
    f.place("needs_mixed", "g2");
    const ScopedEnv probe("FAKE_BLENDER_ADDONS", moduleOf(kAddon) + "," + moduleOf(kMixed, "user_default"));
    std::unique_ptr<DirectoryLink> link;
    std::unique_ptr<ScopedEnv> variable;
    std::unique_ptr<CurrentFolder> here;
    bool mayProbe = false;
    fs::path hold = f.s.extensions;

    SECTION("a repository") { f.request.addonSource = f.s.extensions / kRepository; }
    SECTION("the folder above") { f.request.addonSource = f.s.user.userRoot; }
    SECTION("config")
    {
        f.request.addonSource = f.s.user.userRoot / "config";
        writeAddon(f.request.addonSource);
    }
    SECTION("the AppData Blender folder")
    {
        f.request.addonSource = f.s.root / "appdata" / "Blender Foundation";
        writeAddon(f.request.addonSource);
    }
    SECTION("relative")
    {
        f.request.addonSource = "extensions";
        here = std::make_unique<CurrentFolder>(f.s.user.userRoot);
    }
    SECTION("a junction to extensions")
    {
        link = std::make_unique<DirectoryLink>(f.s.root / "work" / "linked", f.s.extensions);
        f.request.addonSource = f.s.root / "work" / "linked";
    }
    SECTION("extensions that are a junction")
    {
        hold = f.s.root / "other" / "extensions";
        writeAddon(hold);
        link = std::make_unique<DirectoryLink>(f.s.root / "work" / "linked", hold);
        variable = std::make_unique<ScopedEnv>("BLENDER_USER_EXTENSIONS", pathToUtf8(f.s.root / "work" / "linked"));
        f.request.addonSource = f.s.root / "work" / "linked";
        mayProbe = true;
    }
    SECTION("extensions that are config")
    {
        variable = std::make_unique<ScopedEnv>("BLENDER_USER_CONFIG", pathToUtf8(f.s.extensions));
        mayProbe = true;
    }

    const Names before = f.s.state();
    HeldList held;
    holdAll(held, hold);
    const BuildReport report = f.build();
    held.clear();

    requireOk(report.status);
    requireBuildWarning(report);
    if (!mayProbe) REQUIRE(jobCount(f.s.exe, "probe") == 0);
    requireNothingCopied(f);
    REQUIRE(report.counts.generators == 2);
    REQUIRE(f.s.state() == before);
}

TEST_CASE("a failed copy warns once and the build goes on", "[addons][build][safety]")
{
    AddonBuild f("addonbuild-copy-failed");
    f.place("needs_test", "g1");
    f.place("needs_mixed", "g2");
    const ScopedEnv probe("FAKE_BLENDER_ADDONS", moduleOf(kAddon) + "," + moduleOf(kMixed, "user_default"));
    std::unique_ptr<HeldOpen> held;
    EnvList moved;

    SECTION("a file that cannot be read")
    {
        held = std::make_unique<HeldOpen>(f.s.extensions / "user_default" / kMixed / "__init__.py");
    }
    SECTION("a temp folder inside config")
    {
        const fs::path temp = f.s.user.userRoot / "config" / "temp";
        fs::create_directories(temp);
        for (const char* name : {"TEMP", "TMP"}) moved.push_back(std::make_unique<ScopedEnv>(name, pathToUtf8(temp)));
    }

    const BuildReport report = f.build();
    held.reset();
    requireOk(report.status);
    requireBuildWarning(report);
    requireNothingCopied(f);
    REQUIRE(namesIn(f.s.user.userRoot / "config" / "temp").empty());
    REQUIRE(report.counts.generators == 2);
}

TEST_CASE("read-only add-on files leave no job folder behind", "[addons][build]")
{
    AddonBuild f("addonbuild-readonly");
    f.place("needs_test", "g1");
    const ScopedEnv probe("FAKE_BLENDER_ADDONS", moduleOf(kAddon));
    const fs::path file = f.s.extensions / kRepository / kAddon / "__init__.py";
    REQUIRE(SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY));
    const BuildReport report = f.build();
    const DWORD after = GetFileAttributesW(file.c_str());
    SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);

    requireOk(report.status);
    REQUIRE((after & FILE_ATTRIBUTE_READONLY) != 0);
    REQUIRE(extensionsOf(f.s.exe, "build").at("marker") == true);
    REQUIRE(f.jobFolders().empty());
}

TEST_CASE("a failed probe copies nothing and leaves the cache as it was", "[addons][build][safety]")
{
    for (const char* mode : {"fail", "unreadable", "crash", "bad_result", "hang"}) {
        DYNAMIC_SECTION(mode)
        {
            AddonBuild f("addonbuild-probe-failed");
            f.place("needs_test", "g1");
            f.place("needs_mixed", "g2");
            f.save({moduleOf(kAddon)});
            const std::string saved = readTextFile(addonCacheFile());
            const ScopedEnv probe("FAKE_BLENDER_ADDONS", moduleOf(kAddon));
            f.setMode(mode);
            f.request.run.timeout = std::chrono::seconds(5);

            const Names before = f.s.state();
            HeldList held;
            holdAll(held, f.s.extensions);
            const auto started = std::chrono::steady_clock::now();
            const BuildReport report = f.build(atBlender([&] { f.setMode("ok"); }));
            const auto took = std::chrono::steady_clock::now() - started;
            held.clear();

            REQUIRE(took < std::chrono::seconds(60));
            requireOk(report.status);
            REQUIRE(jobCount(f.s.exe, "probe") == 1);
            requireNothingCopied(f);
            requireBuildWarning(report);
            // The saved cache is not used either, so the item it says is off still reaches the worker.
            REQUIRE(itemsInJob(f) == Names{"g1", "g2"});
            REQUIRE(readTextFile(addonCacheFile()) == saved);
            REQUIRE(f.s.state() == before);
        }
    }
}

TEST_CASE("a cancelled probe cancels the build", "[addons][build][safety]")
{
    AddonBuild f("addonbuild-probe-cancelled");
    f.place("needs_test", "g1");
    f.save({moduleOf(kAddon)});
    const std::string saved = readTextFile(addonCacheFile());
    f.setMode("hang");

    std::stop_source stop;
    std::atomic<bool> over{false};
    std::thread stopper([&] {
        for (int i = 0; i < 600 && !over; ++i) {
            if (!fakeLog(f.s.exe, "fake_extensions.log").empty()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        stop.request_stop();
    });
    BuildReport report;
    try {
        requireFakeUser(f.s);
        report = runBuild(f.request, {}, stop.get_token());
    } catch (...) {
        over = true;
        stopper.join();
        throw;
    }
    over = true;
    stopper.join();

    REQUIRE(report.status.code == ErrorCode::Cancelled);
    REQUIRE(jobCount(f.s.exe, "probe") == 1);
    REQUIRE(jobCount(f.s.exe, "build") == 0);
    REQUIRE(f.jobFolders().empty());
    REQUIRE(readTextFile(addonCacheFile()) == saved);
    REQUIRE_FALSE(fs::exists(f.request.projectFolder / "build" / "build.lock"));
}

TEST_CASE("a build that cannot take the lock asks and copies nothing", "[addons][build]")
{
    AddonBuild f("addonbuild-locked");
    f.place("needs_test", "g1");
    const ProcessIdentity me = currentProcess();
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
    writeText(f.request.projectFolder / "build" / "build.lock",
              std::to_string(me.pid) + " " + std::to_string(me.startKey) + " " + std::to_string(now.count()) + "\n");
    const ScopedEnv probe("FAKE_BLENDER_ADDONS", moduleOf(kAddon));

    REQUIRE(f.build().status.code == ErrorCode::InUse);
    REQUIRE(fakeLog(f.s.exe, "fake_args.log").empty());
    REQUIRE(f.jobFolders().empty());
}

namespace {

// Sets the write time of a file, folder or junction itself to some minutes ago.
void makeOld(const fs::path& path, int minutes)
{
    const HANDLE handle = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    REQUIRE(handle != INVALID_HANDLE_VALUE);
    FILETIME time{};
    GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    value.QuadPart -= static_cast<ULONGLONG>(minutes) * 60 * 10000000ULL;
    time.dwLowDateTime = value.LowPart;
    time.dwHighDateTime = value.HighPart;
    const BOOL set = SetFileTime(handle, &time, &time, &time);
    CloseHandle(handle);
    REQUIRE(set);
}

}

TEST_CASE("the next build removes old job folders and nothing else", "[addons][build][safety]")
{
    AddonBuild f("addonbuild-left-over");
    f.place("plain", "g1");

    SECTION("a build with a source") {}
    SECTION("a build without a source") { f.request.addonSource.clear(); }

    const fs::path temp = f.s.user.tempFolder;
    const int old = static_cast<int>(std::chrono::duration_cast<std::chrono::minutes>(kBuildTimeout).count()) * 2 + 10;
    const int newer = old - 20;
    const fs::path target = f.s.root / "other" / "target";
    writeText(target / "keep.txt", "keep");
    makeOld(target / "keep.txt", old);
    makeOld(target, old);

    const fs::path gone = temp / "plynth-ext-0a1b2c3d";
    writeText(gone / "user_default" / kAddon / "file.txt", "left");
    REQUIRE(SetFileAttributesW((gone / "user_default" / kAddon / "file.txt").c_str(), FILE_ATTRIBUTE_READONLY));
    const DirectoryLink inner(gone / "linked", target);
    makeOld(gone, old);
    const DirectoryLink named(temp / "plynth-ext-11112222", target);
    makeOld(temp / "plynth-ext-11112222", old);

    writeText(temp / "plynth-ext-33334444" / "file.txt", "new");
    writeText(temp / "plynth-ext-55556666" / "file.txt", "not old enough");
    makeOld(temp / "plynth-ext-55556666", newer);
    Names keep{"plynth-ext-33334444", "plynth-ext-55556666"};
    for (const char* name : {"plynth-ext-0a1b2c3", "plynth-ext-0a1b2c3d4", "plynth-ext-zzzzzzzz", "plynth-ext-0a1b2c3d-copy",
                             "my-plynth-ext-0a1b2c3d", "plynth-ext_0a1b2c3d"}) {
        writeText(temp / name / "file.txt", "other name");
        makeOld(temp / name, old);
        keep.push_back(name);
    }
    writeText(temp / "plynth-ext-77778888", "a file, not a folder");
    makeOld(temp / "plynth-ext-77778888", old);
    keep.push_back("plynth-ext-77778888");

    requireOk(f.build().status);

    REQUIRE_FALSE(fs::exists(gone));
    for (const std::string& name : keep) {
        INFO(name);
        REQUIRE(fs::exists(temp / name));
        if (fs::is_directory(temp / name)) REQUIRE(fs::exists(temp / name / "file.txt"));
    }
    REQUIRE(readTextFile(target / "keep.txt") == "keep");
}

TEST_CASE("the report keeps the generators count and reads 0 without one", "[build][addonfolder]")
{
    AddonBuild f("addonbuild-count");

    SECTION("no generator items") {}
    SECTION("a failed build") { f.setMode("fail"); }
    f.build();

    const fs::path file = f.request.projectFolder / "build" / "build.json";
    nlohmann::json report = nlohmann::json::parse(readTextFile(file));
    REQUIRE(report.at("counts").contains("generators"));
    REQUIRE(report.at("counts").at("generators") == 0);

    report["counts"]["generators"] = 4;
    writeText(file, report.dump());
    const BuildReportRead counted = readBuildReport(f.request.projectFolder);
    requireOk(counted.status);
    REQUIRE(counted.report.counts.generators == 4);

    for (const nlohmann::json& bad : {nlohmann::json(-1), nlohmann::json(1.5), nlohmann::json("2"), nlohmann::json(nullptr)}) {
        report["counts"]["generators"] = bad;
        writeText(file, report.dump());
        const BuildReportRead refused = readBuildReport(f.request.projectFolder);
        CHECK(refused.status.code == ErrorCode::BadFile);
        CHECK(refused.status.where == "/counts/generators");
    }

    report["counts"].erase("generators");
    writeText(file, report.dump());
    const BuildReportRead older = readBuildReport(f.request.projectFolder);
    requireOk(older.status);
    REQUIRE(older.report.counts.generators == 0);
}

namespace {

// Runs real Blender with six threads, offline, and the given user variables. Returns its output on failure.
std::string runRealBlender(const fs::path& program, const fs::path& script, const Names& after,
                           std::vector<std::pair<std::string, std::string>> environment)
{
    ProcessSpec spec;
    spec.program = program;
    spec.arguments = {"--background", "--factory-startup", "--offline-mode", "-t", "6", "-noaudio",
                      "--python-exit-code", "3", "--python", pathToUtf8(script)};
    spec.arguments.insert(spec.arguments.end(), after.begin(), after.end());
    spec.environment = std::move(environment);
    spec.timeout = std::chrono::minutes(5);
    const ProcessResult done = runProcess(spec);
    if (done.status.ok() && done.exitCode == 0) return {};
    return done.status.message + "\n" + done.outputTail;
}

// The five user variables below one folder, with the extensions folder given apart.
std::vector<std::pair<std::string, std::string>> userVariables(const fs::path& base, const fs::path& extensions)
{
    for (const char* name : {"config", "scripts", "datafiles"}) fs::create_directories(base / name);
    fs::create_directories(extensions);
    return {{"BLENDER_USER_RESOURCES", pathToUtf8(base)},
            {"BLENDER_USER_CONFIG", pathToUtf8(base / "config")},
            {"BLENDER_USER_SCRIPTS", pathToUtf8(base / "scripts")},
            {"BLENDER_USER_EXTENSIONS", pathToUtf8(extensions)},
            {"BLENDER_USER_DATAFILES", pathToUtf8(base / "datafiles")}};
}

}

TEST_CASE("the steps of a build probe, copy and run an add-on in real Blender", "[.blender]")
{
    // A variable of its own, because PLYNTH_BLENDER names a fake inside the test program.
    const fs::path program = variablePath("PLYNTH_TEST_REAL_BLENDER");
    INFO("Set PLYNTH_TEST_REAL_BLENDER to blender.exe to run this case.");
    REQUIRE_FALSE(program.empty());

    AddonBuild f("addonbuild-real");
    writeText(f.library.generators / "needs_test.py",
              "\"\"\"plynth-generator\n"
              R"({"format": "plynth-generator", "version": 1, "name": "Test part", "description": "A part for tests.", )"
              R"("addons": ["plynth_test_addon"], "size": {"width": 0.5, "depth": 0.5, "height": 1.0}, )"
              R"("params": [{"id": "amount", "label": "Amount", "type": "number", "default": 1.0}]})"
              "\n\"\"\"\n\ndef generate(gen):\n"
              "    v = [(x * 0.25, y * 0.25, z) for z in (0, 1) for x, y in ((-1, -1), (1, -1), (1, 1), (-1, 1))]\n"
              "    f = [[3, 2, 1, 0], [4, 5, 6, 7], [0, 1, 5, 4], [1, 2, 6, 5], [2, 3, 7, 6], [3, 0, 4, 7]]\n"
              "    return [gen.mesh_object('box', v, f)]\n");
    f.place("needs_test", "g1");
    requireFakeUser(f.s);

    const fs::path enable = f.s.root / "work" / "enable.py";
    writeText(enable,
              "import addon_utils\nimport bpy\n"
              "name = 'bl_ext.blender_org.plynth_test_addon'\n"
              "assert addon_utils.enable(name, default_set=True, persistent=True) is not None\n"
              "bpy.ops.wm.save_userpref()\n");
    REQUIRE(runRealBlender(program, enable, {}, userVariables(f.s.user.userRoot, f.s.extensions)) == "");
    // Blender keeps caches of its own in the fake profile, so only the settings and the add-on are compared.
    const fs::path config = f.s.user.userRoot / "config";
    const Names settingsBefore = treeState(config);
    const Names addonBefore = treeState(f.s.extensions / kRepository / kAddon);

    const fs::path worker = fs::path(PLYNTH_SCHEMA_DIR).parent_path() / "blender" / "plynth_worker.py";
    const fs::path work = f.s.root / "work" / "run";
    const fs::path shared = f.s.root / "private";
    const Json probeJob = makeProbeJob(blenderUserFolders(f.s.install), "probe-00000001", work / "result.json");
    writeText(work / "job.json", probeJob.dump());
    REQUIRE(runRealBlender(program, worker, {"--", "--job", pathToUtf8(work / "job.json")},
                           userVariables(shared, shared / "extensions")) == "");
    const ProbeResult answer = readProbeResult(Json::parse(readTextFile(work / "result.json")), "probe-00000001");
    requireOk(answer.header.status);
    const AddonCache cache = addonCacheFromProbe(f.s.install, answer, sessionNow());
    REQUIRE(addonEnabled(cache, kAddon));

    const fs::path own = f.s.user.tempFolder / "plynth-ext-0a1b2c3d";
    const auto started = std::chrono::steady_clock::now();
    const AddonFolderCopy copy = copyAddons(f.s, own, f.s.extensions, cache, {kAddon});
    const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    requireOk(copy.status);
    WARN("The copy took " << took.count() << " ms.");

    const GeneratorLibrary generators = loadGeneratorFolder(f.library.generators);
    const BuildAssetSource noAssets = [](std::string_view) -> std::optional<BuildAssetState> { return std::nullopt; };
    const BuildPlan plan = makeBuildPlan(f.request.project, noAssets, {}, &generators, cache);
    const Json job = makeBuildJob(f.request.project, plan, BuildSettings{}, f.request.projectFolder, "build-0a1b2c3d",
                                  work / "built.json");
    writeText(work / "build-job.json", job.dump());
    fs::create_directories(f.request.projectFolder / "build");
    REQUIRE(runRealBlender(program, worker, {"--", "--job", pathToUtf8(work / "build-job.json")},
                           userVariables(shared, own)) == "");
    const BuildWorkerOutput out = readBuildResult(Json::parse(readTextFile(work / "built.json")), "build-0a1b2c3d");
    requireOk(out.header.status);
    REQUIRE(out.counts.generators == 1);
    REQUIRE(treeState(config) == settingsBefore);
    REQUIRE(treeState(f.s.extensions / kRepository / kAddon) == addonBefore);
    REQUIRE_FALSE(f.s.sharedMarked());
}

namespace {

// The arguments of each job the fake Blender was started for, as one list per start.
std::vector<Names> jobArguments(const fs::path& exe)
{
    std::vector<Names> starts;
    for (const std::string& line : fakeLog(exe, "fake_args.log")) {
        Names words;
        for (std::size_t at = 0;;) {
            const std::size_t end = line.find('|', at);
            words.push_back(line.substr(at, end == std::string::npos ? end : end - at));
            if (end == std::string::npos) break;
            at = end + 1;
        }
        if (std::find(words.begin(), words.end(), "--job") != words.end()) starts.push_back(std::move(words));
    }
    return starts;
}

// The arguments that come before --python.
Names beforePython(const Names& words)
{
    const auto python = std::find(words.begin(), words.end(), "--python");
    REQUIRE(python != words.end());
    return Names(words.begin(), python);
}

// How often a word is in a list.
int timesListed(const Names& words, const std::string& word)
{
    return static_cast<int>(std::count(words.begin(), words.end(), word));
}

}

TEST_CASE("factory startup always goes with offline mode", "[addons][blenderjob][safety]")
{
    AddonScratch s("addonjob-offline");
    BlenderRunOptions options;
    options.scriptsFolder = s.scripts;
    const std::string threads{kBlenderThreadsVariable};
    const ScopedEnv cap(threads, "");
    Json job = smallBuildJob(s);
    int offline = 1;

    SECTION("a build") {}
    SECTION("a build with a thread cap") { _wputenv_s(widenUtf8(threads).c_str(), L"6"); }
    SECTION("a build with a folder of its own") { options.extensionsFolder = markedFolder(s.user.tempFolder / "own"); }
    SECTION("normal startup")
    {
        options.factoryStartup = false;
        offline = 0;
    }
    SECTION("a probe, also when normal startup is asked")
    {
        options.factoryStartup = false;
        requireFakeUser(s);
        job = makeProbeJob(blenderUserFolders(s.install), "probe-00000001", s.root / "work" / "run" / "result.json");
    }

    requireOk(runFakeJob(s, job, options).status);
    const std::vector<Names> starts = jobArguments(s.exe);
    REQUIRE(starts.size() == 1);
    REQUIRE(timesListed(starts[0], "--offline-mode") == offline);
    REQUIRE(timesListed(beforePython(starts[0]), "--offline-mode") == offline);
    REQUIRE(timesListed(beforePython(starts[0]), "--factory-startup") == offline);
}

TEST_CASE("asking for the version never asks for offline mode", "[addons][blenderjob]")
{
    AddonScratch s("addonjob-version");
    const ScopedEnv cap(std::string(kBlenderThreadsVariable), "6");
    requireFakeUser(s);
    REQUIRE(inspectBlender(s.exe).usable());
    const Names lines = fakeLog(s.exe, "fake_args.log");
    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0].find("--offline-mode") == std::string::npos);
    REQUIRE(lines[0].find("|-t|") == std::string::npos);
}

TEST_CASE("the probe and the job of a build go offline and use the scripts of the request", "[addons][build][safety]")
{
    AddonBuild f("addonbuild-offline");
    f.place("needs_test", "g1");
    const ScopedEnv probe("FAKE_BLENDER_ADDONS", moduleOf(kAddon));
    f.request.run.scriptsFolder = makeFakeScripts(f.s.root / "own-scripts");
    f.request.run.factoryStartup = false;

    requireOk(f.build().status);
    const std::vector<Names> starts = jobArguments(f.s.exe);
    REQUIRE(starts.size() == 2);
    for (const Names& words : starts) {
        REQUIRE(timesListed(beforePython(words), "--offline-mode") == 1);
        const auto python = std::find(words.begin(), words.end(), "--python");
        REQUIRE(python + 1 != words.end());
        REQUIRE(spelling(pathFromUtf8(*(python + 1))) == spelling(f.s.root / "own-scripts" / "plynth_worker.py"));
    }
}

TEST_CASE("the thread cap goes before the script of every job", "[addons][blenderjob]")
{
    for (const char* value : {"6", "1", "1024"}) {
        for (const char* kind : {"build", "probe", "normal startup"}) {
            DYNAMIC_SECTION(kind << " " << value)
            {
                AddonScratch s("addonjob-threads");
                BlenderRunOptions options;
                options.scriptsFolder = s.scripts;
                const std::string named = kind;
                Json job = smallBuildJob(s);
                if (named == "probe") {
                    requireFakeUser(s);
                    job = makeProbeJob(blenderUserFolders(s.install), "probe-00000001", s.root / "work" / "run" / "result.json");
                }
                if (named == "normal startup") options.factoryStartup = false;
                const ScopedEnv cap(std::string(kBlenderThreadsVariable), value);

                requireOk(runFakeJob(s, job, options).status);
                const std::vector<Names> starts = jobArguments(s.exe);
                REQUIRE(starts.size() == 1);
                const Names early = beforePython(starts[0]);
                REQUIRE(timesListed(starts[0], "-t") == 1);
                const auto flag = std::find(early.begin(), early.end(), "-t");
                REQUIRE(flag != early.end());
                REQUIRE(flag + 1 != early.end());
                REQUIRE(*(flag + 1) == value);
                REQUIRE(timesListed(early, "--offline-mode") == (named == "normal startup" ? 0 : 1));
            }
        }
    }
}

TEST_CASE("an unset and an empty thread cap add nothing", "[addons][blenderjob]")
{
    AddonScratch s("addonjob-no-threads");
    BlenderRunOptions options;
    options.scriptsFolder = s.scripts;
    const std::string threads{kBlenderThreadsVariable};
    const Json job = smallBuildJob(s);
    const ScopedEnv cap(threads, "");

    requireOk(runFakeJob(s, job, options).status);
    // The runtime cannot hold an empty variable, so only the Windows environment gets one.
    REQUIRE(SetEnvironmentVariableW(widenUtf8(threads).c_str(), L""));
    requireOk(runFakeJob(s, job, options).status);

    const std::vector<Names> starts = jobArguments(s.exe);
    REQUIRE(starts.size() == 2);
    REQUIRE(starts[0] == starts[1]);
    REQUIRE(timesListed(starts[0], "-t") == 0);
    REQUIRE(timesListed(beforePython(starts[0]), "--offline-mode") == 1);
}

TEST_CASE("a bad thread cap starts nothing", "[addons][blenderjob][safety]")
{
    for (const char* value : {"0", "-1", "6.5", "6x", " 6", "6 ", "+6", "0x6", "1025", "99999999999999999999"}) {
        for (const char* kind : {"build", "probe", "normal startup", "a build that would copy"}) {
            DYNAMIC_SECTION(kind << " [" << value << "]")
            {
                AddonBuild f("addonjob-bad-threads");
                f.place("needs_test", "g1");
                BlenderRunOptions options;
                options.scriptsFolder = f.s.scripts;
                const std::string named = kind;
                const ScopedEnv cap(std::string(kBlenderThreadsVariable), value);

                Status status;
                if (named == "a build that would copy") {
                    status = f.build().status;
                } else {
                    Json job = smallBuildJob(f.s);
                    if (named == "probe") {
                        requireFakeUser(f.s);
                        job = makeProbeJob(blenderUserFolders(f.s.install), "probe-00000001",
                                           f.s.root / "work" / "run" / "result.json");
                    }
                    if (named == "normal startup") options.factoryStartup = false;
                    status = runFakeJob(f.s, job, options).status;
                }
                REQUIRE(status.code == ErrorCode::InvalidValue);
                REQUIRE(fakeLog(f.s.exe, "fake_args.log").empty());
                REQUIRE(f.jobFolders().empty());
            }
        }
    }
}

namespace {

// Leaves empty the variables that would choose a user folder before the portable and AppData ones.
EnvList clearedUserVariables()
{
    EnvList cleared;
    for (const char* name : {"BLENDER_USER_RESOURCES", "BLENDER_USER_DATAFILES"}) {
        cleared.push_back(std::make_unique<ScopedEnv>(name, ""));
    }
    return cleared;
}

// The case of a job folder that is refused before anything is made.
void requireJobRefused(const AddonScratch& s, const fs::path& job)
{
    INFO(pathToUtf8(job));
    const bool existed = fs::exists(job);
    const Names before = s.state();
    const Names privateBefore = treeState(s.root / "private");
    const AddonFolderCopy copy = copyTestAddon(s, job, s.extensions);
    REQUIRE(copy.status.code == ErrorCode::InvalidValue);
    REQUIRE(copy.copied.empty());
    REQUIRE(fs::exists(job) == existed);
    REQUIRE(s.state() == before);
    REQUIRE(treeState(s.root / "private") == privateBefore);
    REQUIRE_FALSE(s.sharedMarked());
}

}

TEST_CASE("the datafiles folder is found like the others", "[addons][addonfolder]")
{
    struct Way {
        const char* name;
        const char* folder;
    };
    for (const Way& way : {Way{"variable", "user/datafiles"}, Way{"resources", "resources/datafiles"},
                           Way{"portable", "blender/portable/datafiles"},
                           Way{"appdata", "appdata/Blender Foundation/Blender/5.2/datafiles"},
                           Way{"variable to nothing", "user/datafiles"}}) {
        DYNAMIC_SECTION(way.name)
        {
            AddonScratch s("addonfolder-datafiles");
            const std::string found = way.name;
            EnvList cleared;
            std::unique_ptr<ScopedEnv> chosen;
            if (found == "variable to nothing") {
                chosen = std::make_unique<ScopedEnv>("BLENDER_USER_DATAFILES", pathToUtf8(s.root / "none"));
            } else if (found != "variable") {
                cleared = clearedUserVariables();
                if (found == "resources") {
                    fs::create_directories(s.root / "resources");
                    chosen = std::make_unique<ScopedEnv>("BLENDER_USER_RESOURCES", pathToUtf8(s.root / "resources"));
                }
                if (found != "appdata") fs::create_directories(s.root / "blender" / "portable");
            }
            requireFakeUser(s);
            REQUIRE(spelling(blenderUserFolders(s.install).datafiles) == spelling(s.root / way.folder));
        }
    }
}

TEST_CASE("the probe job still has four folders", "[addons][addonfolder]")
{
    AddonScratch s("addonfolder-probe-job");
    requireFakeUser(s);
    const BlenderUserFolders folders = blenderUserFolders(s.install);
    REQUIRE_FALSE(folders.datafiles.empty());
    const nlohmann::json job =
        nlohmann::json::parse(makeProbeJob(folders, "probe-00000001", s.root / "work" / "result.json").dump());
    Names keys;
    for (const auto& entry : job.at("params").at("user_folders").items()) keys.push_back(entry.key());
    std::sort(keys.begin(), keys.end());
    REQUIRE(keys == Names{"blenderkit_data", "config", "extensions", "scripts"});
    REQUIRE(schemaProblem("blender-job.schema.json", job).empty());
}

TEST_CASE("extensions that overlap datafiles are no source", "[addons][addonfolder][safety]")
{
    AddonScratch s("addonfolder-datafiles-source");
    requireCopyWorks(s);
    EnvList cleared;
    std::unique_ptr<ScopedEnv> extensions;
    std::unique_ptr<ScopedEnv> datafiles;
    std::unique_ptr<DirectoryLink> link;
    fs::path source = s.extensions;
    const auto point = [&](const fs::path& folder) {
        fs::create_directories(folder);
        datafiles = std::make_unique<ScopedEnv>("BLENDER_USER_DATAFILES", pathToUtf8(folder));
    };

    SECTION("is datafiles") { point(source); }
    SECTION("holds datafiles") { point(source / "datafiles"); }
    SECTION("lies in datafiles")
    {
        source = s.user.userRoot / "datafiles" / "extensions";
        extensions = std::make_unique<ScopedEnv>("BLENDER_USER_EXTENSIONS", pathToUtf8(source));
    }
    SECTION("is datafiles found by resources")
    {
        cleared = clearedUserVariables();
        source = s.root / "resources" / "datafiles";
        extensions = std::make_unique<ScopedEnv>("BLENDER_USER_EXTENSIONS", pathToUtf8(source));
        datafiles = std::make_unique<ScopedEnv>("BLENDER_USER_RESOURCES", pathToUtf8(s.root / "resources"));
    }
    SECTION("datafiles resolves into it")
    {
        link = std::make_unique<DirectoryLink>(s.root / "work" / "datafiles", source);
        point(s.root / "work" / "datafiles");
    }
    writeAddon(source);
    requireFakeUser(s);
    requireRefused(s, source, source);
}

TEST_CASE("a job folder in or around the private folder or datafiles is refused", "[addons][addonfolder][safety]")
{
    AddonScratch s("addonfolder-job-private");
    requireCopyWorks(s);
    EnvList cleared;
    std::unique_ptr<DirectoryLink> link;
    std::unique_ptr<ScopedEnv> variable;
    fs::path job;

    SECTION("in the private folder") { job = s.root / "private" / "job"; }
    SECTION("in the shared extensions") { job = s.root / "private" / "extensions" / "job"; }
    SECTION("in the private folder in capitals") { job = upperSpelling(s.root / "private") / "job"; }
    SECTION("in the private folder through a junction")
    {
        link = std::make_unique<DirectoryLink>(s.root / "work" / "linked", s.root / "private");
        job = s.root / "work" / "linked" / "job";
    }
    SECTION("in what a private junction points at")
    {
        link = std::make_unique<DirectoryLink>(s.root / "work" / "linked", s.root / "other" / "shared");
        variable = std::make_unique<ScopedEnv>("PLYNTH_BLENDER_PRIVATE", pathToUtf8(s.root / "work" / "linked"));
        fs::create_directories(s.root / "other" / "shared");
        job = s.root / "other" / "shared" / "job";
    }
    SECTION("in a private folder not yet made")
    {
        variable = std::make_unique<ScopedEnv>("PLYNTH_BLENDER_PRIVATE", pathToUtf8(s.root / "other" / "later"));
        job = s.root / "other" / "later" / "job";
    }
    SECTION("a private folder not yet made")
    {
        job = s.root / "other" / "later";
        variable = std::make_unique<ScopedEnv>("PLYNTH_BLENDER_PRIVATE", pathToUtf8(job));
    }
    SECTION("around a private folder not yet made")
    {
        job = s.root / "other" / "holder";
        variable = std::make_unique<ScopedEnv>("PLYNTH_BLENDER_PRIVATE", pathToUtf8(job / "private"));
    }
    SECTION("around the private folder")
    {
        // A folder that holds one is there already, so it is refused either way and left as it was.
        job = s.root / "other" / "holder";
        fs::create_directories(job / "private");
        variable = std::make_unique<ScopedEnv>("PLYNTH_BLENDER_PRIVATE", pathToUtf8(job / "private"));
    }
    SECTION("in datafiles found by its variable") { job = s.user.userRoot / "datafiles" / "job"; }
    SECTION("around datafiles found by its variable")
    {
        job = s.root / "other" / "holder";
        fs::create_directories(job / "datafiles");
        variable = std::make_unique<ScopedEnv>("BLENDER_USER_DATAFILES", pathToUtf8(job / "datafiles"));
    }
    SECTION("in datafiles found by resources")
    {
        cleared = clearedUserVariables();
        fs::create_directories(s.root / "resources");
        variable = std::make_unique<ScopedEnv>("BLENDER_USER_RESOURCES", pathToUtf8(s.root / "resources"));
        job = s.root / "resources" / "datafiles" / "job";
    }
    SECTION("datafiles found by resources and not yet made")
    {
        cleared = clearedUserVariables();
        fs::create_directories(s.root / "resources");
        variable = std::make_unique<ScopedEnv>("BLENDER_USER_RESOURCES", pathToUtf8(s.root / "resources"));
        job = s.root / "resources" / "datafiles";
    }
    SECTION("around datafiles found by resources")
    {
        cleared = clearedUserVariables();
        job = s.root / "other" / "holder";
        fs::create_directories(job);
        variable = std::make_unique<ScopedEnv>("BLENDER_USER_RESOURCES", pathToUtf8(job));
    }
    requireJobRefused(s, job);
}

TEST_CASE("the default private folder takes no job folder", "[addons][addonfolder][safety]")
{
    AddonScratch s("addonfolder-job-default");
    requireCopyWorks(s);
    requireFakeUser(s);
    const ScopedEnv unset("PLYNTH_BLENDER_PRIVATE", "");
    REQUIRE(insideFolder(variablePath("LOCALAPPDATA"), s.root));
    const fs::path job = s.root / "local" / "Plynth" / "blender-private" / "job";
    const AddonFolderCopy copy = prepareAddonFolder(job, s.extensions, s.install, s.cache({moduleOf(kAddon)}), {kAddon});
    REQUIRE(copy.status.code == ErrorCode::InvalidValue);
    REQUIRE_FALSE(fs::exists(job.parent_path()));
}

TEST_CASE("the Blender Foundation name is refused for a job folder by both functions", "[addons][addonfolder][blenderjob][safety]")
{
    struct Named {
        const char* name;
        const char* below;
        bool linked;
        bool refused;
    };
    for (const Named& named : {Named{"Blender Foundation", "job", false, true}, Named{"Blender Foundation", "", false, true},
                               Named{"BLENDER FOUNDATION", "job", false, true}, Named{"blender foundation", "a/b/job", false, true},
                               Named{"Blender Foundation.", "job", false, true}, Named{"Blender Foundation ", "", false, true},
                               Named{"Blender Foundation. .", "", false, true}, Named{"Blender Foundation", "job", true, true},
                               Named{"Blender Foundation copy", "job", false, false}}) {
        for (const bool launch : {false, true}) {
            DYNAMIC_SECTION("[" << named.name << "/" << named.below << "] linked " << named.linked << " launch " << launch)
            {
                AddonScratch s("addonfolder-job-name");
                requireCopyWorks(s);
                requireFakeUser(s);
                const fs::path parent = s.root / "other" / "elsewhere";
                fs::create_directories(parent);
                fs::path job = parent / named.name;
                if (*named.below) job /= named.below;

                std::unique_ptr<DirectoryLink> link;
                if (named.linked) {
                    fs::create_directories(parent / named.name);
                    link = std::make_unique<DirectoryLink>(s.root / "work" / "linked", parent / named.name);
                    job = s.root / "work" / "linked" / named.below;
                }
                if (launch) {
                    BlenderRunOptions options;
                    options.scriptsFolder = s.scripts;
                    options.extensionsFolder = job;
                    // Windows drops dots and spaces at the end, so the folder on disk is named without them.
                    std::string onDisk = named.name;
                    while (onDisk.back() == '.' || onDisk.back() == ' ') onDisk.pop_back();
                    fs::path made = parent / onDisk;
                    if (*named.below) made /= named.below;
                    markedFolder(made);
                    const Json buildJob = smallBuildJob(s);
                    const Names before = treeState(parent);
                    const BlenderJobRun run = runFakeJob(s, buildJob, options);
                    if (named.refused) {
                        REQUIRE(run.status.code == ErrorCode::InvalidValue);
                        REQUIRE(fakeLog(s.exe, "fake_args.log").empty());
                        REQUIRE(treeState(parent) == before);
                    } else {
                        requireOk(run.status);
                    }
                } else {
                    const Names before = treeState(parent);
                    const AddonFolderCopy copy = copyTestAddon(s, job, s.extensions);
                    if (named.refused) {
                        REQUIRE(copy.status.code == ErrorCode::InvalidValue);
                        REQUIRE(treeState(parent) == before);
                    } else {
                        requireOk(copy.status);
                    }
                }
            }
        }
    }
}

TEST_CASE("separators at the end are dropped on both sides", "[addons][addonfolder]")
{
    for (const char* end : {"/", "\\", "//", "\\\\", "/\\", "\\/"}) {
        for (const char* side : {"source", "variable", "both"}) {
            DYNAMIC_SECTION(side << " [" << end << "]")
            {
                AddonScratch s("addonfolder-separator");
                writeAddon(s.extensions);
                const std::string where = side;
                const std::string plain = pathToUtf8(s.extensions);
                const ScopedEnv variable("BLENDER_USER_EXTENSIONS", where == "source" ? plain : plain + end);
                const fs::path source = pathFromUtf8(where == "variable" ? plain : plain + end);
                const fs::path job = s.user.tempFolder / "job";
                const AddonFolderCopy copy = copyTestAddon(s, job, source);
                requireOk(copy.status);
                REQUIRE(copy.copied == Names{kAddon});
                REQUIRE(fs::is_regular_file(job / kMarker));
            }
        }
    }
}

TEST_CASE("a separator at the end forgives nothing else", "[addons][addonfolder][safety]")
{
    AddonScratch s("addonfolder-separator-only");
    requireCopyWorks(s);
    const std::wstring slash = L"/";

    SECTION("a dot") { requireRefused(s, fs::path(s.extensions.wstring() + L"." + slash), s.extensions); }
    SECTION("a space") { requireRefused(s, fs::path(s.extensions.wstring() + L" " + slash), s.extensions); }
    SECTION("the long path prefix")
    {
        const std::wstring prefix = L"\\\\?\\";
        requireRefused(s, fs::path(prefix + fs::path(s.extensions).make_preferred().wstring() + L"\\"), s.extensions);
    }
    SECTION("a short name")
    {
        // Only where the volume keeps short names.
        if (const fs::path shorter = shortSpelling(s.extensions); !shorter.empty()) {
            requireRefused(s, fs::path(shorter.wstring() + slash), s.extensions);
        }
    }
}

TEST_CASE("a build takes a source with a separator at its end", "[addons][build]")
{
    AddonBuild f("addonbuild-separator");
    f.place("needs_test", "g1");
    const ScopedEnv probe("FAKE_BLENDER_ADDONS", moduleOf(kAddon));
    f.request.addonSource = pathFromUtf8(pathToUtf8(f.s.extensions) + "/");

    requireOk(f.build().status);
    REQUIRE(jobCount(f.s.exe, "probe") == 1);
    REQUIRE(extensionsOf(f.s.exe, "build").at("marker") == true);
}
