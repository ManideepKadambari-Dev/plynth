#include "plynth/core/EditSession.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace plynth {

namespace {

// Reads the creation time of a process as one number. False when Windows refuses.
bool creationKey(HANDLE process, long long& key)
{
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return false;
    ULARGE_INTEGER value;
    value.LowPart = created.dwLowDateTime;
    value.HighPart = created.dwHighDateTime;
    key = static_cast<long long>(value.QuadPart);
    return true;
}

}

// The id and creation time of this process.
ProcessIdentity currentProcess()
{
    ProcessIdentity identity;
    identity.pid = static_cast<long long>(GetCurrentProcessId());
    creationKey(GetCurrentProcess(), identity.startKey);
    return identity;
}

// Says whether that exact run of a process is still going.
bool isProcessRunning(const ProcessIdentity& process)
{
    if (process.pid <= 0 || process.pid > 0xFFFFFFFFLL) return false;
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(process.pid));
    if (!handle) return GetLastError() == ERROR_ACCESS_DENIED;

    bool running = false;
    DWORD exitCode = 0;
    if (GetExitCodeProcess(handle, &exitCode) && exitCode == STILL_ACTIVE) {
        running = true;
        if (process.startKey != 0) {
            long long key = 0;
            running = creationKey(handle, key) && key == process.startKey;
        }
    }
    CloseHandle(handle);
    return running;
}

}
