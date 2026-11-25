#include "UpdateInternals.h"

#include "plynth/core/UpdateNames.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace plynth::update_detail {

namespace fs = std::filesystem;

namespace {

// Says whether a mutex of that name exists in this session.
bool mutexExists(const wchar_t* name)
{
    const HANDLE handle = OpenMutexW(SYNCHRONIZE, FALSE, name);
    if (!handle) return false;
    CloseHandle(handle);
    return true;
}

// The folder of the running program.
fs::path ownFolder()
{
    std::wstring buffer(32768, L'\0');
    const DWORD got = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (got == 0 || got >= buffer.size()) return {};
    buffer.resize(got);
    return fs::path(buffer).parent_path();
}

}

// Marks a packaged program as running, unless setup runs.
Status beginPackagedRunIn(const fs::path& programFolder, const wchar_t* runningMutex, const wchar_t* setupMutex)
{
    std::error_code ec;
    if (programFolder.empty() || !fs::exists(programFolder / kPackageFile, ec)) return {};
    const Status busy = fail(ErrorCode::UpdateInProgress,
                             "Plynth is being installed or updated right now. Start it again when the installer has finished.");
    if (mutexExists(setupMutex)) return busy;
    // The handle is kept open on purpose: the mark lasts until the process ends.
    const HANDLE running = CreateMutexW(nullptr, FALSE, runningMutex);
    if (!running) return {};
    if (mutexExists(setupMutex)) {
        CloseHandle(running);
        return busy;
    }
    return {};
}

}

namespace plynth {

// Marks this packaged program as running and, for an installed copy, removes old downloads.
Status beginPackagedRun()
{
    using namespace update_detail;
    const Status status = beginPackagedRunIn(ownFolder(), kRunningMutex, kSetupMutex);
    if (!status.ok()) return status;
    if (environment().installation().kind == InstallKind::Installed) {
        UpdateStateFile state;
        if (readUpdateState(stateFile(), state).ok()) {
            removeOldDownloads(downloadRoot(), state.last ? state.last->log.parent_path() : fs::path());
        }
    }
    return {};
}

}
