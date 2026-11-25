#include "UpdateInternals.h"

#include "plynth/core/EditSession.h"
#include "plynth/core/ProjectFiles.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <vector>

namespace plynth::update_detail {

namespace fs = std::filesystem;

// The installer's command line in update mode.
std::wstring installerCommandLine(const fs::path& program, const fs::path& log, bool reopen)
{
    std::wstring line = L"\"" + program.wstring() + L"\" /SILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER "
                        L"/NOCLOSEAPPLICATIONS /NORESTARTAPPLICATIONS /PLYNTHUPDATE=1";
    if (reopen) line += L" /PLYNTHREOPEN=1";
    line += L" /LOG=\"" + log.wstring() + L"\"";
    return line;
}

// Starts the installer with only its own file handle inherited, outside this process's job where it may leave.
Status startInstallerProcess(const InstallerStart& start)
{
    HANDLE inherited = static_cast<HANDLE>(start.inheritedHandle);
    if (!inherited || !SetHandleInformation(inherited, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
        return fail(ErrorCode::CannotRead, "The installer file could not be passed to the installer.");
    }
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<unsigned char> storage(size);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &size)) {
        SetHandleInformation(inherited, HANDLE_FLAG_INHERIT, 0);
        return fail(ErrorCode::CannotRead, "The installer could not be started.");
    }
    struct Cleanup {
        LPPROC_THREAD_ATTRIBUTE_LIST list;
        HANDLE handle;
        ~Cleanup()
        {
            DeleteProcThreadAttributeList(list);
            SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0);
        }
    } cleanup{attributes, inherited};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &inherited, sizeof inherited,
                                   nullptr, nullptr)) {
        return fail(ErrorCode::CannotRead, "The installer could not be started.");
    }

    STARTUPINFOEXW info{};
    info.StartupInfo.cb = sizeof info;
    info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    info.StartupInfo.hStdInput = nullptr;
    info.StartupInfo.hStdOutput = nullptr;
    info.StartupInfo.hStdError = nullptr;
    info.lpAttributeList = attributes;

    const JobPlacement job = currentJobPlacement();
    const DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS;
    const auto launch = [&](DWORD extra) {
        std::wstring line = start.commandLine;
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(start.program.c_str(), line.data(), nullptr, nullptr, TRUE, flags | extra, nullptr,
                            start.workingFolder.c_str(), &info.StartupInfo, &process)) {
            return GetLastError();
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return static_cast<DWORD>(ERROR_SUCCESS);
    };
    DWORD error = launch(job.inJob ? CREATE_BREAKAWAY_FROM_JOB : 0);
    if (error == ERROR_ACCESS_DENIED && job.inJob) {
        if (job.killOnClose) {
            return fail(ErrorCode::UpdateBlocked, "The program that started Plynth would end the installer together "
                                                  "with Plynth. Start Plynth from the Start menu or a normal terminal "
                                                  "and update again.");
        }
        error = launch(0);
    }
    if (error != ERROR_SUCCESS) {
        return fail(ErrorCode::CannotRead, "The installer could not be started (error " + std::to_string(error) + ").");
    }
    return {};
}

}

namespace plynth {

// Starts the verified installer in update mode and records the start.
Status launchUpdate(VerifiedInstaller&& installer, const std::filesystem::path& currentFolder, bool reopen)
{
    using namespace update_detail;
    VerifiedInstaller own = std::move(installer);
    const UpdateEnvironment& env = environment();
    const Installation installation = env.installation();
    if (installation.kind != InstallKind::Installed) {
        return fail(ErrorCode::UpdateNotInstalled, std::string(kNotInstalledMessage) + " " + std::string(kReleasesPage));
    }
    void* handle = InstallerAccess::handle(own);
    if (!handle) return fail(ErrorCode::InvalidValue, "This installer was already used.");
    const std::vector<UpdateBlocker> blockers = updateBlockers(installation, currentFolder);
    if (!blockers.empty()) {
        std::string message;
        for (const UpdateBlocker& blocker : blockers) message += (message.empty() ? "" : " ") + blocker.message;
        return fail(ErrorCode::UpdateBlocked, message);
    }

    UpdateStateFile before;
    if (Status s = readUpdateState(stateFile(), before); !s.ok()) return s;
    UpdateStateFile after = before;
    const fs::path log = own.file().parent_path() / "install.log";
    after.last = UpdateLast{installation.version, own.version(), formatSessionTime(sessionNow()), log};
    if (Status s = writeUpdateState(stateFile(), after); !s.ok()) return s;

    InstallerStart start;
    start.program = own.file();
    start.commandLine = installerCommandLine(own.file(), log, reopen);
    start.workingFolder = own.file().parent_path();
    start.inheritedHandle = handle;
    const Status started = env.startInstaller ? env.startInstaller(start) : startInstallerProcess(start);
    if (!started.ok()) writeUpdateState(stateFile(), before);
    return started;
}

}
