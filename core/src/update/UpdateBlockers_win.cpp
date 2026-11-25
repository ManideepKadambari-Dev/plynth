#include "UpdateInternals.h"

#include "BuildLockFiles.h"

#include "plynth/core/EditSession.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/Settings.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

namespace plynth::update_detail {

namespace fs = std::filesystem;

namespace {

// A file name in lower case.
std::wstring lowerName(std::wstring name)
{
    if (!name.empty()) CharLowerBuffW(name.data(), static_cast<DWORD>(name.size()));
    return name;
}

// Says whether a program file name is plynth.exe or plynth.com, ignoring case.
bool isPlynthName(const std::wstring& name)
{
    const std::wstring lower = lowerName(name);
    return lower == L"plynth.exe" || lower == L"plynth.com";
}

// The full path of a running program. Empty when it cannot be read.
fs::path imageOf(DWORD pid)
{
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    std::wstring buffer(32768, L'\0');
    DWORD size = static_cast<DWORD>(buffer.size());
    fs::path result;
    if (QueryFullProcessImageNameW(process, 0, buffer.data(), &size)) {
        buffer.resize(size);
        result = fs::path(buffer);
    }
    CloseHandle(process);
    return result;
}

// Says whether a program is a Plynth program from the install folder or its cli folder.
bool fromInstall(const RunningProgram& program, const fs::path& installFolder)
{
    if (program.image.empty() || !isPlynthName(program.image.filename().wstring())) return false;
    const fs::path folder = program.image.parent_path();
    return sameFolder(folder, installFolder) || sameFolder(folder, installFolder / "cli");
}

}

// The programs running now. Only Plynth programs get their full path read.
std::vector<RunningProgram> runningPrograms()
{
    std::vector<RunningProgram> programs;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return programs;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        RunningProgram program;
        program.pid = entry.th32ProcessID;
        program.parentPid = entry.th32ParentProcessID;
        if (isPlynthName(entry.szExeFile)) program.image = imageOf(entry.th32ProcessID);
        programs.push_back(std::move(program));
    }
    CloseHandle(snapshot);
    return programs;
}

// How the current process sits in a job. A job that cannot be read counts as one that ends its programs.
JobPlacement currentJobPlacement()
{
    JobPlacement placement;
    BOOL inJob = FALSE;
    if (!IsProcessInJob(GetCurrentProcess(), nullptr, &inJob) || !inJob) return placement;
    placement.inJob = true;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    if (!QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &info, sizeof info, nullptr)) {
        placement.killOnClose = true;
        return placement;
    }
    const DWORD flags = info.BasicLimitInformation.LimitFlags;
    placement.killOnClose = (flags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) != 0;
    placement.breakawayAllowed = (flags & (JOB_OBJECT_LIMIT_BREAKAWAY_OK | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)) != 0;
    return placement;
}

// The blockers from given facts.
std::vector<UpdateBlocker> blockersFrom(const BlockerFacts& facts)
{
    std::vector<UpdateBlocker> blockers;
    if (facts.installation.kind != InstallKind::Installed) {
        blockers.push_back({"not_installed", std::string(kNotInstalledMessage)});
        return blockers;
    }
    unsigned long launcher = 0;
    for (const RunningProgram& program : facts.programs) {
        if (program.pid == facts.selfPid) launcher = program.parentPid;
    }
    for (const RunningProgram& program : facts.programs) {
        if (program.pid == facts.selfPid || !fromInstall(program, facts.installation.folder)) continue;
        if (program.pid == launcher && lowerName(program.image.filename().wstring()) == L"plynth.com") continue;
        blockers.push_back({"plynth_running", "Another Plynth is running from the install folder (" +
                                                  pathToUtf8(program.image.filename()) + ", process " +
                                                  std::to_string(program.pid) +
                                                  "). Close every other Plynth window and command, the built-in "
                                                  "terminal included, then update again."});
    }

    std::vector<fs::path> seen;
    const SessionTime now = sessionNow();
    for (const fs::path& project : facts.projects) {
        if (project.empty()) continue;
        bool repeated = false;
        for (const fs::path& other : seen) repeated = repeated || sameFolder(other, project);
        if (repeated) continue;
        seen.push_back(project);
        if (readSession(project, now).state == SessionState::Active) {
            blockers.push_back({"edit_session", "An edit session is active in " + pathToUtf8(project) +
                                                    ". End it with plynth session end, then update again."});
        }
        const fs::path lock = project / "build" / std::string(kBuildLockFileName);
        std::error_code ec;
        if (fs::exists(lock, ec) && lockfile::lockIsLive(lock)) {
            blockers.push_back({"build_running", "A build or export is running in " + pathToUtf8(project) +
                                                     ". Wait for it to finish, then update again."});
        }
    }

    if (facts.job.inJob && facts.job.killOnClose && !facts.job.breakawayAllowed) {
        blockers.push_back({"parent_job", "The program that started Plynth would end the installer together with "
                                          "Plynth. Start Plynth from the Start menu or a normal terminal and update "
                                          "again."});
    }
    return blockers;
}

}

namespace plynth {

// Every reason the update cannot start now.
std::vector<UpdateBlocker> updateBlockers(const Installation& installation, const std::filesystem::path& currentFolder)
{
    using namespace update_detail;
    const UpdateEnvironment& env = environment();
    if (env.blockers) return env.blockers(installation, currentFolder);
    BlockerFacts facts;
    facts.installation = installation;
    if (installation.kind == InstallKind::Installed) {
        facts.programs = runningPrograms();
        facts.selfPid = GetCurrentProcessId();
        facts.projects.push_back(currentFolder);
        const SettingsLoad settings = loadSettings();
        if (settings.status.ok()) {
            for (const RecentProject& recent : settings.settings.recentProjects) facts.projects.push_back(recent.path);
        }
        facts.job = currentJobPlacement();
    }
    return blockersFrom(facts);
}

}
