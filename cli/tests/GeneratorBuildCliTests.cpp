#include "FakeBlender.h"
#include "MaterialLibrary.h"
#include "SampleProject.h"
#include "SettingsGuard.h"

#include <plynth/core/Addons.h>
#include <plynth/core/Blender.h>
#include <plynth/core/EditSession.h>
#include <plynth/core/Library.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Settings.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <string>
#include <thread>
#include <vector>

using plynth::cli::kExitBadArguments;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::installFakeBlender;
using plynth::cli::testing::isInsideFolder;
using plynth::cli::testing::Outcome;
using plynth::cli::testing::PrivateUser;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;
using plynth::cli::testing::ScopedEnvironment;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

namespace fs = std::filesystem;
using Words = std::vector<std::string>;
using EnvList = std::vector<std::unique_ptr<ScopedEnvironment>>;

const std::string kTestAddon = "plynth_test_addon";

// The value of an environment variable, read without touching any folder.
fs::path envPath(const char* name) {
    const std::wstring wide(name, name + std::char_traits<char>::length(name));
    const DWORD size = GetEnvironmentVariableW(wide.c_str(), nullptr, 0);
    if (size == 0) {
        return {};
    }
    std::wstring value(size, L'\0');
    value.resize(GetEnvironmentVariableW(wide.c_str(), value.data(), size));
    return fs::path(value);
}

// Writes a file, making its folder.
void writeText(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
}

// Writes a generator with the given parameters and add-ons.
void writeGenerator(const fs::path& folder, const std::string& id, const std::string& addons = "[]",
                    const std::string& params =
                        R"({"id": "amount", "label": "Amount", "type": "number", "default": 1.0})") {
    writeText(folder / (id + ".py"),
              "\"\"\"plynth-generator\n"
              R"({"format": "plynth-generator", "version": 1, "name": "Test part", "description": "A part for tests.", )"
              R"("addons": )" + addons + R"(, "size": {"width": 0.5, "depth": 0.5, "height": 1.0}, "params": [)" + params +
                  "]}\n\"\"\"\n\ndef generate(gen):\n    return []\n");
}

// Writes a small add-on made for these tests.
fs::path writeAddon(const fs::path& extensions, const std::string& id = kTestAddon) {
    const fs::path folder = extensions / "blender_org" / id;
    writeText(folder / "blender_manifest.toml",
              "schema_version = \"1.0.0\"\nid = \"" + id + "\"\nversion = \"1.0.0\"\ntype = \"add-on\"\n");
    writeText(folder / "__init__.py", "def register():\n    pass\n");
    return folder;
}

// A folder and everything in it, held open so that any other open to read fails.
class HeldTree {
public:
    explicit HeldTree(const fs::path& folder) {
        std::vector<fs::path> paths{folder};
        for (const auto& entry : fs::recursive_directory_iterator(folder)) {
            paths.push_back(entry.path());
        }
        for (auto it = paths.rbegin(); it != paths.rend(); ++it) {
            HANDLE handle = INVALID_HANDLE_VALUE;
            for (int attempt = 0; attempt < 40 && handle == INVALID_HANDLE_VALUE; ++attempt) {
                handle = CreateFileW(it->c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
                if (handle == INVALID_HANDLE_VALUE) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
            REQUIRE(handle != INVALID_HANDLE_VALUE);
            handles_.push_back(handle);
        }
    }
    ~HeldTree() {
        for (const HANDLE handle : handles_) {
            CloseHandle(handle);
        }
    }
    HeldTree(const HeldTree&) = delete;
    HeldTree& operator=(const HeldTree&) = delete;

private:
    std::vector<HANDLE> handles_;
};

// Every entry below a folder with its write time and content.
Words folderState(const fs::path& folder) {
    Words out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::string line = plynth::pathToUtf8(it->path().lexically_relative(folder));
        if (it->is_regular_file()) {
            line += "|" + std::to_string(it->last_write_time().time_since_epoch().count()) + "|" + readFile(it->path());
        }
        out.push_back(line);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// A made-up user, settings, samples, temp folder and fake Blender, all in one temporary folder.
struct CliScratch {
    explicit CliScratch(const std::string& name) : temp(name), user(temp) {
        root = temp.path();
        tmp = root / "tmp";
        for (const fs::path& folder : {tmp / "config", root / "local", root / "home", root / "samples" / "shapes",
                                       root / "samples" / "generators"}) {
            fs::create_directories(folder);
        }
        fake = installFakeBlender(root / "fb", "5.2.0");
        writeText(root / "scripts" / "plynth_worker.py", "# stand in for the worker\n");
        set("TEMP", tmp);
        set("TMP", tmp);
        set("PLYNTH_CONFIG_DIR", tmp / "config");
        set("LOCALAPPDATA", root / "local");
        set("HOME", root / "home");
        set("PLYNTH_SAMPLES", root / "samples");
        set("PLYNTH_BLENDER", fake);
        set("PLYNTH_BLENDER_SCRIPTS", root / "scripts");
        for (const char* variable : {"FAKE_BLENDER_MODE", "FAKE_BLENDER_ADDONS", "FAKE_BLENDER_BLENDERKIT", "PLYNTH_TEST_ADDONS"}) {
            set(variable, {});
        }
        extensions = root / "user" / "extensions";
        plynth::cli::testing::requireSafeSettings("starting a generator build test");
    }

    // Points one variable at a path of this test.
    void set(const char* variable, const fs::path& value) {
        envs.push_back(std::make_unique<ScopedEnvironment>(variable, plynth::pathToUtf8(value)));
    }

    TempFolder temp;
    PrivateUser user;
    EnvList envs;
    fs::path root;
    fs::path tmp;
    fs::path fake;
    fs::path extensions;
};

// Stops the case unless every user folder of this Blender lies in the temporary folder. The variables are
// judged first, so no folder outside is looked at.
void requireFakeUser(const CliScratch& s, const fs::path& blender) {
    for (const char* name : {"APPDATA", "LOCALAPPDATA", "USERPROFILE", "HOME", "XDG_DATA_HOME", "TEMP", "TMP",
                             "PLYNTH_BLENDER_PRIVATE", "PLYNTH_CONFIG_DIR"}) {
        INFO(name);
        REQUIRE(isInsideFolder(envPath(name), s.root));
    }
    for (const char* name : {"BLENDER_USER_RESOURCES", "BLENDER_USER_CONFIG", "BLENDER_USER_SCRIPTS",
                             "BLENDER_USER_EXTENSIONS", "BLENDER_USER_DATAFILES"}) {
        INFO(name);
        const fs::path value = envPath(name);
        REQUIRE((value.empty() || isInsideFolder(value, s.root)));
    }
    REQUIRE(isInsideFolder(blender, s.root));
    plynth::BlenderInstall install;
    install.executable = blender;
    install.version = plynth::BlenderVersion{5, 2, 0};
    const plynth::BlenderUserFolders folders = plynth::blenderUserFolders(install);
    for (const fs::path& folder : {folders.config, folders.scripts, folders.extensions, folders.blenderKitData}) {
        REQUIRE(isInsideFolder(folder, s.root));
    }
    REQUIRE((folders.datafiles.empty() || isInsideFolder(folders.datafiles, s.root)));
    REQUIRE(isInsideFolder(plynth::addonCacheFile(), s.root));
}

// Removes the logs of a fake Blender, so only what follows is judged.
void clearLogs(const fs::path& exe) {
    std::error_code ec;
    for (const char* name : {"fake_args.log", "fake_env.log", "fake_jobs.log", "fake_extensions.log"}) {
        fs::remove(exe.parent_path() / name, ec);
    }
}

// The JSON lines of one log of a fake Blender that are about a task.
std::vector<nlohmann::json> loggedFor(const fs::path& exe, const char* log, const std::string& task) {
    std::vector<nlohmann::json> found;
    std::ifstream in(exe.parent_path() / log, std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        const nlohmann::json entry = nlohmann::json::parse(line);
        if (entry.at("task") == task) {
            found.push_back(entry);
        }
    }
    return found;
}

// Says whether this fake Blender was ever started.
bool wasStarted(const fs::path& exe) {
    return fs::exists(exe.parent_path() / "fake_args.log");
}

// The problems with a code in a build report or check result.
std::vector<nlohmann::json> problemsWith(const nlohmann::json& result, const std::string& code) {
    std::vector<nlohmann::json> found;
    for (const auto& problem : result.at("problems")) {
        if (problem.at("code") == code) {
            found.push_back(problem);
        }
    }
    return found;
}

// The job folders left in the temp folder.
Words jobFolders(const CliScratch& s) {
    Words found;
    for (const auto& entry : fs::directory_iterator(s.tmp)) {
        const std::string name = plynth::pathToUtf8(entry.path().filename());
        if (name.rfind("plynth-ext-", 0) == 0) {
            found.push_back(name);
        }
    }
    return found;
}

// Saves an add-on cache of a Blender.
void saveCache(const CliScratch& s, const fs::path& blender, const Words& modules) {
    REQUIRE(isInsideFolder(plynth::addonCacheFile(), s.root));
    plynth::AddonCache cache;
    cache.blender = blender;
    cache.blenderVersion = "5.2.0";
    cache.checkedAt = plynth::sessionNow();
    for (const std::string& module : modules) {
        cache.enabled.push_back({module, module, "1.0.0"});
    }
    REQUIRE(plynth::writeAddonCache(plynth::addonCacheFile(), cache).ok());
}

// A project with one room and a library in the settings that has a plain generator and one that needs an add-on.
struct CliBuild {
    explicit CliBuild(const std::string& name) : s(name), sample("project") {
        library = s.root / "Library";
        REQUIRE(runChecked({"library", "init", utf8(library), "--use"}).exitCode == kExitOk);
        writeGenerator(library / "generators", "plain_part");
        writeGenerator(library / "generators", "addon_part", R"(["plynth_test_addon"])");
        sample.drawRoomSquare();
    }

    // Places a generator item in the room and returns its id.
    std::string place(const std::string& generator, const Words& more = {}) const {
        Words args{"place", "--generator", generator, "--x", "3", "--y", "5"};
        args.insert(args.end(), more.begin(), more.end());
        const Outcome placed = sample.run(args);
        REQUIRE(placed.exitCode == kExitOk);
        return placed.answer.at("result").at("item").at("id").get<std::string>();
    }

    CliScratch s;
    Sample sample;
    fs::path library;
};

}

TEST_CASE("the CLI test program keeps every user folder in its own scratch root", "[cli][safety]") {
    // Nothing here opens or looks for a folder, so it is safe also when the guard is not there.
    const fs::path temp = envPath("TEMP").lexically_normal();
    const fs::path root = (temp.has_filename() ? temp : temp.parent_path()).parent_path();
    // The setup names the root after this process, so the parent of some other temp folder never passes.
    const std::string rootName = plynth::pathToUtf8(root.filename());
    INFO(rootName);
    REQUIRE(std::regex_match(rootName, std::regex("plynth-" + std::to_string(_getpid()) + "-(0|[1-9][0-9]*)")));
    for (const char* name : {"APPDATA", "LOCALAPPDATA", "USERPROFILE", "HOME", "XDG_DATA_HOME", "PLYNTH_BLENDER_PRIVATE",
                             "PLYNTH_CONFIG_DIR", "BLENDER_USER_RESOURCES", "BLENDER_USER_CONFIG", "BLENDER_USER_SCRIPTS",
                             "BLENDER_USER_EXTENSIONS", "BLENDER_USER_DATAFILES", "TMP"}) {
        INFO(name);
        REQUIRE(isInsideFolder(envPath(name), root));
    }
    plynth::BlenderInstall install;
    install.executable = root / "blender" / "blender.exe";
    install.version = plynth::BlenderVersion{5, 2, 0};
    const plynth::BlenderUserFolders folders = plynth::blenderUserFolders(install);
    for (const fs::path& folder : {folders.config, folders.scripts, folders.extensions, folders.blenderKitData}) {
        REQUIRE(isInsideFolder(folder, root));
    }
    REQUIRE((folders.datafiles.empty() || isInsideFolder(folders.datafiles, root)));
}

TEST_CASE("library init and library info list the generators", "[cli][library][generator]") {
    const CliScratch s("genbuild-info");
    const fs::path samples = s.root / "samples" / "generators";
    writeGenerator(samples, "zed_part");
    writeGenerator(samples, "alpha_part");
    const fs::path library = s.root / "Library";

    const Outcome made = runChecked({"library", "init", utf8(library), "--use"});
    REQUIRE(made.exitCode == kExitOk);
    REQUIRE(made.answer.at("result").contains("generators"));
    CHECK(made.answer.at("result").at("generators").get<Words>() == Words{"alpha_part", "zed_part"});

    writeGenerator(library / "generators", "middle_part");
    writeText(library / "generators" / "broken.py", "print('no declaration')\n");
    writeText(library / "generators" / "_helper.py", "x = 1\n");
    const Outcome info = runChecked({"library", "info"});
    REQUIRE(info.exitCode == kExitOk);
    CHECK(info.answer.at("result").at("generators").get<Words>() == Words{"alpha_part", "middle_part", "zed_part"});
}

TEST_CASE("library init without sample generators lists none", "[cli][library][generator]") {
    const CliScratch s("genbuild-info-none");
    const Outcome made = runChecked({"library", "init", utf8(s.root / "Library")});
    REQUIRE(made.exitCode == kExitOk);
    REQUIRE(made.answer.at("result").contains("generators"));
    CHECK(made.answer.at("result").at("generators").empty());
}

TEST_CASE("library add-sample-generators needs no --from", "[cli][library][generator]") {
    const CliScratch s("genbuild-from");
    const fs::path library = s.root / "Library";
    REQUIRE(runChecked({"library", "init", utf8(library)}).exitCode == kExitOk);
    writeGenerator(s.root / "samples" / "generators", "late_part");

    const Outcome added = runChecked({"library", "add-sample-generators", "--library", utf8(library)});
    REQUIRE(added.exitCode == kExitOk);
    CHECK(added.answer.at("result").at("copied").get<Words>() == Words{"late_part.py"});
    CHECK(fs::exists(library / "generators" / "late_part.py"));

    const Outcome again = runChecked({"library", "add-sample-generators", "--library", utf8(library)});
    REQUIRE(again.exitCode == kExitOk);
    CHECK(again.answer.at("result").at("copied").empty());
    CHECK(again.answer.at("result").at("skipped").get<Words>() == Words{"late_part.py"});
}

TEST_CASE("library add-sample-generators says where it looked when nothing is found", "[cli][library][generator]") {
    const CliScratch s("genbuild-from-none");
    const fs::path library = s.root / "Library";
    REQUIRE(runChecked({"library", "init", utf8(library)}).exitCode == kExitOk);
    const ScopedEnvironment samples("PLYNTH_SAMPLES", plynth::pathToUtf8(s.root / "none"));

    const Outcome outcome = runChecked({"library", "add-sample-generators", "--library", utf8(library)});
    if (plynth::sampleGeneratorsFolder().empty()) {
        REQUIRE(outcome.exitCode == kExitFailed);
        CHECK(errorCode(outcome) == "not_found");
        const std::string message = outcome.answer.at("error").at("message").get<std::string>();
        CHECK(message.find("--from") != std::string::npos);
        CHECK(message.find("PLYNTH_SAMPLES") != std::string::npos);
    } else {
        // A developer build finds the source tree.
        REQUIRE(outcome.exitCode == kExitOk);
    }
}

TEST_CASE("an empty --blender is refused and starts nothing", "[cli][generator][safety]") {
    for (const bool fromSettings : {false, true}) {
        std::vector<Words> commands{{"build"},
                                    {"build", "--check"},
                                    {"export", "--check"},
                                    {"generator", "list"},
                                    {"generator", "list", "--probe"},
                                    {"generator", "check-addons"},
                                    {"library", "rescan"}};
        // Without the variable doctor would search this computer for every Blender, so it is only tried with it.
        if (!fromSettings) {
            commands.push_back({"doctor"});
        }
        for (const Words& command : commands) {
            for (const bool joined : {false, true}) {
                std::string title = fromSettings ? "settings" : "variable";
                for (const std::string& word : command) {
                    title += " " + word;
                }
                title += joined ? " joined" : " apart";
                DYNAMIC_SECTION(title) {
                    const CliBuild f("genbuild-empty-blender");
                    f.place("plain_part");
                    auto loaded = plynth::loadSettings();
                    REQUIRE(loaded.status.ok());
                    loaded.settings.blender = f.s.fake;
                    REQUIRE(plynth::saveSettings(loaded.settings).ok());
                    std::unique_ptr<ScopedEnvironment> variable;
                    if (fromSettings) {
                        variable = std::make_unique<ScopedEnvironment>("PLYNTH_BLENDER", "");
                    }
                    requireFakeUser(f.s, f.s.fake);
                    clearLogs(f.s.fake);

                    Words args = command;
                    if (joined) {
                        args.push_back("--blender=");
                    } else {
                        args.push_back("--blender");
                        args.push_back("");
                    }
                    const bool onProject = command.front() == "build" || command.front() == "export";
                    const Outcome outcome = onProject ? f.sample.run(args) : runChecked(args);

                    CHECK(outcome.exitCode == kExitBadArguments);
                    CHECK(errorCode(outcome) == "bad_arguments");
                    CHECK_FALSE(wasStarted(f.s.fake));
                }
            }
        }
    }
}

TEST_CASE("material list counts the materials of generators as used", "[cli][material][generator]") {
    const CliBuild f("genbuild-materials");
    writeGenerator(f.library / "generators", "finished_part", "[]",
                   R"({"id": "finish", "label": "Finish", "type": "material", "default": null},)"
                   R"( {"id": "grain", "label": "Grain", "type": "choice", "default": "gold_leaf",)"
                   R"( "choices": [{"id": "gold_leaf", "label": "Gold"}]})");
    plynth::cli::testing::makeMaterial(f.library, "silver_leaf", "Silver leaf", {"metal"}, {"leaf"});
    f.place("finished_part", {"--param", "finish=silver_leaf"});
    f.place("finished_part", {"--param", "finish=green_marble"});

    const Outcome listed = f.sample.run({"material", "list"});
    REQUIRE(listed.exitCode == kExitOk);
    const Words used = listed.answer.at("result").at("used").get<Words>();
    const Words missing = listed.answer.at("result").at("missing").get<Words>();
    CHECK(std::ranges::count(used, std::string("silver_leaf")) == 1);
    CHECK(std::ranges::count(used, std::string("green_marble")) == 1);
    CHECK(std::ranges::count(used, std::string("gold_leaf")) == 0);
    CHECK(std::ranges::is_sorted(used));
    CHECK(std::ranges::count(missing, std::string("green_marble")) == 1);
    CHECK(std::ranges::count(missing, std::string("silver_leaf")) == 0);

    plynth::cli::testing::removeSettingsFileSafely();
    const Outcome alone = f.sample.run({"material", "list"});
    REQUIRE(alone.exitCode == kExitOk);
    CHECK(std::ranges::count(alone.answer.at("result").at("used").get<Words>(), std::string("silver_leaf")) == 0);
}

TEST_CASE("build --check finds a missing generator and starts nothing", "[cli][build][generator]") {
    const CliBuild f("genbuild-check");
    const std::string item = f.place("plain_part");
    std::string code = "missing_generator";

    SECTION("missing") { fs::remove(f.library / "generators" / "plain_part.py"); }
    SECTION("broken") {
        writeText(f.library / "generators" / "plain_part.py", "print('no declaration')\n");
        code = "generator_broken";
    }
    SECTION("there") { code.clear(); }
    clearLogs(f.s.fake);

    const Outcome outcome = f.sample.run({"build", "--check"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    CHECK(problemsWith(result, "missing_asset").empty());
    if (code.empty()) {
        CHECK(result.at("error_count") == 0);
    } else {
        const auto found = problemsWith(result, code);
        REQUIRE(found.size() == 1);
        CHECK(found[0].at("object") == item);
        CHECK(found[0].at("severity") == "error");
        CHECK(found[0].at("step") == "check");
        CHECK(result.at("error_count") == 1);
    }
    CHECK_FALSE(wasStarted(f.s.fake));
    CHECK_FALSE(fs::exists(f.sample.folder / "build"));
}

TEST_CASE("build through the fake counts the generators", "[cli][build][generator]") {
    const CliBuild f("genbuild-count");
    const std::string item = f.place("plain_part");
    clearLogs(f.s.fake);

    const Outcome outcome = f.sample.run({"build", "--quiet"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& counts = outcome.answer.at("result").at("counts");
    REQUIRE(counts.contains("generators"));
    CHECK(counts.at("generators") == 1);
    CHECK(counts.at("items") == 0);
    CHECK(problemsWith(outcome.answer.at("result"), "missing_asset").empty());
    const auto jobs = loggedFor(f.s.fake, "fake_jobs.log", "build");
    REQUIRE(jobs.size() == 1);
    CHECK(jobs[0].at("params").at("floors").at(0).at("generator_items").at(0).at("id") == item);
    CHECK(loggedFor(f.s.fake, "fake_jobs.log", "probe").empty());
    CHECK(jobFolders(f.s).empty());
}

TEST_CASE("the help of build names generators", "[cli][help][generator]") {
    const CliScratch s("genbuild-help");
    const Outcome outcome = runChecked({"help"});
    std::string description;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        if (command.at("command") == "build") {
            description = command.at("description").get<std::string>();
        }
    }
    CHECK(description.find("enerator") != std::string::npos);
}

TEST_CASE("build copies the add-on from the extensions of the fake user", "[cli][build][addons][safety]") {
    const CliBuild f("genbuild-copy");
    writeAddon(f.s.extensions);
    const fs::path decoy = writeAddon(f.s.extensions, "plynth_off");
    f.place("addon_part");
    const ScopedEnvironment addons("FAKE_BLENDER_ADDONS", "bl_ext.blender_org." + kTestAddon);
    requireFakeUser(f.s, f.s.fake);
    const Words before = folderState(f.s.root / "user" / "extensions");
    clearLogs(f.s.fake);

    Outcome outcome;
    {
        const HeldTree held(decoy);
        outcome = f.sample.run({"build", "--quiet"});
    }
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(loggedFor(f.s.fake, "fake_jobs.log", "probe").size() == 1);
    const auto seen = loggedFor(f.s.fake, "fake_extensions.log", "build");
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].at("marker") == true);
    CHECK(seen[0].at("user_default").get<Words>() == Words{kTestAddon});
    CHECK(problemsWith(outcome.answer.at("result"), "generator_unavailable").empty());
    CHECK(outcome.answer.at("result").at("counts").at("generators") == 1);
    CHECK(jobFolders(f.s).empty());
    CHECK(folderState(f.s.root / "user" / "extensions") == before);
}

TEST_CASE("build --blender copies from the extensions of that Blender", "[cli][build][addons][safety]") {
    const CliBuild f("genbuild-portable");
    f.place("addon_part");
    EnvList cleared;
    for (const char* name : {"BLENDER_USER_RESOURCES", "BLENDER_USER_CONFIG", "BLENDER_USER_SCRIPTS", "BLENDER_USER_EXTENSIONS"}) {
        cleared.push_back(std::make_unique<ScopedEnvironment>(name, ""));
    }
    const fs::path option = installFakeBlender(f.s.root / "option", "5.2.0");
    const fs::path saved = installFakeBlender(f.s.root / "saved", "5.2.0");
    const fs::path named = f.s.fake;
    // The name differs only in case, so the log shows which folder was copied.
    const std::string wanted = "Plynth_Test_Addon";
    writeAddon(option.parent_path() / "portable" / "extensions", wanted);
    for (const fs::path& other : {saved, named}) {
        writeAddon(other.parent_path() / "portable" / "extensions");
    }
    auto loaded = plynth::loadSettings();
    REQUIRE(loaded.status.ok());
    loaded.settings.blender = saved;
    REQUIRE(plynth::saveSettings(loaded.settings).ok());
    const ScopedEnvironment addons("FAKE_BLENDER_ADDONS", "bl_ext.blender_org." + wanted);
    for (const fs::path& blender : {option, saved, named}) {
        requireFakeUser(f.s, blender);
        clearLogs(blender);
    }
    const Words before = folderState(option.parent_path() / "portable");

    Outcome outcome;
    {
        const HeldTree heldSaved(saved.parent_path() / "portable" / "extensions");
        const HeldTree heldNamed(named.parent_path() / "portable" / "extensions");
        outcome = f.sample.run({"build", "--quiet", "--blender", utf8(option)});
    }
    REQUIRE(outcome.exitCode == kExitOk);
    const auto seen = loggedFor(option, "fake_extensions.log", "build");
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].at("marker") == true);
    CHECK(seen[0].at("user_default").get<Words>() == Words{wanted});
    CHECK_FALSE(wasStarted(saved));
    CHECK_FALSE(wasStarted(named));
    CHECK(problemsWith(outcome.answer.at("result"), "generator_unavailable").empty());
    CHECK(jobFolders(f.s).empty());
    CHECK(folderState(option.parent_path() / "portable") == before);
}

TEST_CASE("build leaves out an add-on that is off and copies nothing", "[cli][build][addons][safety]") {
    const CliBuild f("genbuild-off");
    const fs::path addon = writeAddon(f.s.extensions);
    const std::string item = f.place("addon_part");
    std::size_t probes = 0;

    SECTION("the saved cache says off") { saveCache(f.s, f.s.fake, {}); }
    SECTION("only the fresh answer says off") {
        saveCache(f.s, f.s.fake, {"bl_ext.blender_org." + kTestAddon});
        probes = 1;
    }
    requireFakeUser(f.s, f.s.fake);
    clearLogs(f.s.fake);

    Outcome outcome;
    {
        const HeldTree held(addon);
        outcome = f.sample.run({"build", "--quiet"});
    }
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(loggedFor(f.s.fake, "fake_jobs.log", "probe").size() == probes);
    const auto seen = loggedFor(f.s.fake, "fake_extensions.log", "build");
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].at("marker") == false);
    CHECK(seen[0].at("user_default").empty());
    const auto found = problemsWith(outcome.answer.at("result"), "generator_unavailable");
    REQUIRE(found.size() == 1);
    CHECK(found[0].at("object") == item);
    CHECK(found[0].at("severity") == "warning");
    bool warned = false;
    for (const auto& warning : outcome.answer.at("warnings")) {
        warned = warned || warning.at("code") == "generator_unavailable";
    }
    CHECK(warned);
    CHECK(outcome.answer.at("result").at("error_count") == 0);
    CHECK(jobFolders(f.s).empty());
}

TEST_CASE("build --check starts nothing and opens nothing in extensions", "[cli][build][addons][safety]") {
    const CliBuild f("genbuild-check-addons");
    writeAddon(f.s.extensions);
    f.place("addon_part");

    SECTION("no cache") {}
    SECTION("a cache that says on") { saveCache(f.s, f.s.fake, {"bl_ext.blender_org." + kTestAddon}); }
    SECTION("a cache that says off") { saveCache(f.s, f.s.fake, {}); }
    requireFakeUser(f.s, f.s.fake);
    const Words before = folderState(f.s.extensions);
    clearLogs(f.s.fake);

    Outcome outcome;
    {
        const HeldTree held(f.s.extensions);
        outcome = f.sample.run({"build", "--check"});
    }
    REQUIRE(outcome.exitCode == kExitOk);
    // The check says nothing about which add-ons are on.
    CHECK(outcome.answer.at("result").at("error_count") == 0);
    CHECK(problemsWith(outcome.answer.at("result"), "generator_unavailable").empty());
    CHECK_FALSE(wasStarted(f.s.fake));
    CHECK(jobFolders(f.s).empty());
    CHECK_FALSE(fs::exists(f.sample.folder / "build"));
    CHECK(folderState(f.s.extensions) == before);
}
