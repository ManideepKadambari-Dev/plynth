// Starts a fake installer the way Plynth does, from inside a job object that ends its programs when it closes,
// for the update tests. Arguments: <installer> <breakaway 0|1> <state file>. With breakaway 1 the job lets
// programs leave it. Prints the status name of the start and exits; the job then closes.
#include "../../../core/src/update/UpdateInternals.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <crtdbg.h>

#include <cstdio>
#include <filesystem>
#include <string>

// Puts itself in a job, starts the installer and reports what happened.
int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    if (argc != 4) {
        std::fputs("usage: update_launcher <installer> <breakaway 0|1> <state file>\n", stderr);
        return 2;
    }
    const std::filesystem::path installer(argv[1]);
    const bool breakaway = std::wstring(argv[2]) == L"1";

    const HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | (breakaway ? JOB_OBJECT_LIMIT_BREAKAWAY_OK : 0);
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits) ||
        !AssignProcessToJobObject(job, GetCurrentProcess())) {
        std::fputs("job_failed\n", stdout);
        return 4;
    }

    void* handle = plynth::update_detail::openLocked(installer);
    if (!handle) {
        std::fputs("open_failed\n", stdout);
        return 5;
    }
    plynth::VerifiedInstaller verified = plynth::update_detail::InstallerAccess::make(handle, installer, "0.2.0", 0);

    plynth::update_detail::UpdateEnvironment env;
    const plynth::Installation installed{plynth::InstallKind::Installed, installer.parent_path(), "0.1.0"};
    env.installation = [installed] { return installed; };
    env.blockers = [](const plynth::Installation&, const std::filesystem::path&) {
        return std::vector<plynth::UpdateBlocker>{};
    };
    env.stateFile = std::filesystem::path(argv[3]);
    const plynth::update_detail::ScopedUpdateEnvironment scoped(env);

    const plynth::Status started = plynth::launchUpdate(std::move(verified), {}, false);
    std::fputs((std::string(plynth::errorCodeName(started.code)) + "\n").c_str(), stdout);
    std::fflush(stdout);
    if (started.ok()) return 0;
    return started.code == plynth::ErrorCode::UpdateBlocked ? 3 : 1;
}
