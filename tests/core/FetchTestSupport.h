#pragma once

#include "CatalogSupport.h"
#include "FakeBlender.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/Process.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/BlenderKit.h>
#include <plynth/core/catalog/StoreFetch.h>
#include <plynth/core/BlenderJob.h>

#include <nlohmann/json.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

namespace plynth::test {

constexpr const char* kFetchItem = "0c1d4a2e-1111-2222-3333-444455556666";

// A made-up user, a short fetch folder named by PLYNTH_FETCH_FOLDER, a fake Blender, its scripts and a work
// folder. Each fetch's record is in fake_fetch.log next to the fake.
struct FetchRig {
    // Makes all of it. The fetch folder is short, so its length stays far below the limit. The fake's folder and
    // the log folder are labelled low: a fetch runs at low integrity, and the fake and the fake helper must be
    // able to write their records there. Everything else in tmp, such as a test's library, stays unlabelled.
    explicit FetchRig(const std::string& tag)
        : fetchFolder(std::filesystem::temp_directory_path() / ("pf" + tag + std::to_string(GetCurrentProcessId()))), user(tag), tmp(tag)
    {
        std::error_code ec;
        std::filesystem::remove_all(fetchFolder, ec);
        folderVariable = std::make_unique<ScopedEnv>("PLYNTH_FETCH_FOLDER", pathToUtf8(fetchFolder));
        requireOk(makeLowIntegrityFolder(tmp.path() / "b"));
        logs = tmp.path() / "l";
        requireOk(makeLowIntegrityFolder(logs));
        blender = makeFakeBlender(tmp.path() / "b", "5.2.0");
        base.scriptsFolder = makeFakeScripts(tmp.path() / "s");
        work = tmp.path() / "w";
    }
    ~FetchRig()
    {
        std::error_code ec;
        std::filesystem::remove_all(fetchFolder, ec);
    }

    // Runs one fetch of the made-up model.
    FetchRun fetch(std::stop_token stop = {}, std::chrono::seconds timeout = std::chrono::seconds(0)) const
    {
        ImportFetch item = makeBlenderKitFetch(kFetchItem, kFetchKindModel, BlenderKitOptions{});
        if (timeout.count() > 0) item.timeout = timeout;
        return runImportFetch(item, blender, base, work, {}, stop);
    }

    // The records of every fetch so far.
    std::vector<nlohmann::json> runs() const
    {
        std::vector<nlohmann::json> out;
        std::ifstream in(blender.parent_path() / "fake_fetch.log", std::ios::binary);
        for (std::string line; std::getline(in, line);) {
            if (!line.empty()) out.push_back(nlohmann::json::parse(line));
        }
        return out;
    }

    // How many times the fake was started at all.
    int starts() const
    {
        std::ifstream in(blender.parent_path() / "fake_args.log", std::ios::binary);
        int count = 0;
        for (std::string line; std::getline(in, line);) ++count;
        return count;
    }

    std::filesystem::path fetchFolder; // made before the user, so it is under the short real temp folder
    IsolatedUser user;
    TempDir tmp;
    std::unique_ptr<ScopedEnv> folderVariable;
    std::filesystem::path blender;
    BlenderRunOptions base;
    std::filesystem::path work;
    std::filesystem::path logs; // labelled low, for logs that programs of the fetch write, such as the helper's
};

// Every file and folder below a folder, relative and with forward slashes. Links are listed, not followed.
inline std::vector<std::string> listing(const std::filesystem::path& folder)
{
    std::vector<std::string> out;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(folder, std::filesystem::directory_options::none, ec), end;
         !ec && it != end; it.increment(ec)) {
        out.push_back(pathToUtf8(it->path().lexically_relative(folder)));
        if (it->is_symlink(ec) || (GetFileAttributesW(it->path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) it.disable_recursion_pending();
    }
    for (std::string& name : out) std::replace(name.begin(), name.end(), '\\', '/');
    std::sort(out.begin(), out.end());
    return out;
}

// Says whether a process with this id still runs.
inline bool processRunning(unsigned long pid)
{
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!process) return false;
    const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return running;
}

// Ends a process that a test left behind.
inline void endProcess(unsigned long pid)
{
    if (const HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid)) {
        TerminateProcess(process, 1);
        WaitForSingleObject(process, 5000);
        CloseHandle(process);
    }
}

// Makes a directory junction from link to target, as an untrusted program could leave in the fetch folder.
inline bool makeJunction(const std::filesystem::path& link, const std::filesystem::path& target)
{
    if (!CreateDirectoryW(link.c_str(), nullptr)) return false;
    const HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const std::wstring substitute = L"\\??\\" + std::filesystem::absolute(target).wstring();
    const std::wstring print = std::filesystem::absolute(target).wstring();
    std::vector<char> buffer(sizeof(DWORD) + 2 * sizeof(WORD) + 4 * sizeof(WORD) + (substitute.size() + print.size() + 2) * sizeof(wchar_t));
    auto* header = reinterpret_cast<DWORD*>(buffer.data());
    header[0] = IO_REPARSE_TAG_MOUNT_POINT;
    auto* words = reinterpret_cast<WORD*>(buffer.data() + sizeof(DWORD));
    const WORD substituteBytes = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    const WORD printBytes = static_cast<WORD>(print.size() * sizeof(wchar_t));
    words[0] = static_cast<WORD>(buffer.size() - 8);
    words[1] = 0;
    words[2] = 0;
    words[3] = substituteBytes;
    words[4] = static_cast<WORD>(substituteBytes + sizeof(wchar_t));
    words[5] = printBytes;
    auto* path = reinterpret_cast<wchar_t*>(buffer.data() + 8 + 4 * sizeof(WORD));
    std::copy(substitute.begin(), substitute.end(), path);
    path[substitute.size()] = 0;
    std::copy(print.begin(), print.end(), path + substitute.size() + 1);
    path[substitute.size() + 1 + print.size()] = 0;
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr, 0, &returned, nullptr);
    CloseHandle(handle);
    return ok != FALSE;
}

// Keeps a process from ending for a while, as a helper that ignores the end would: a thread attaches to it
// as a debugger and answers no debug event, so Windows cannot finish ending it. Afterwards it lets go.
class HoldExit {
public:
    // Starts holding the process, from its own thread, for the given time.
    HoldExit(unsigned long pid, std::chrono::milliseconds hold)
        : thread_([this, pid, hold] {
              attached_ = DebugActiveProcess(pid) != FALSE;
              if (attached_) DebugSetProcessKillOnExit(FALSE);
              ready_.release();
              std::this_thread::sleep_for(hold);
              if (attached_) DebugActiveProcessStop(pid);
          })
    {
        ready_.acquire();
    }
    ~HoldExit() { thread_.join(); }
    HoldExit(const HoldExit&) = delete;
    HoldExit& operator=(const HoldExit&) = delete;

    // Says whether the hold could start.
    bool attached() const { return attached_; }

private:
    std::binary_semaphore ready_{0};
    bool attached_ = false;
    std::thread thread_;
};

// Waits until a file holds a process id, and returns it, or 0 after the time.
inline unsigned long waitForPid(const std::filesystem::path& file, std::chrono::milliseconds limit)
{
    const auto end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end) {
        std::ifstream in(file, std::ios::binary);
        unsigned long pid = 0;
        if (in >> pid && pid != 0) return pid;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return 0;
}

// Says whether any file below a folder holds a made-up secret.
inline bool holdsSecret(const std::filesystem::path& folder)
{
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::ifstream in(it->path(), std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        if (text.find("KEY-MARKER-7f31") != std::string::npos || text.find("TOKEN-MARKER-92ac") != std::string::npos) return true;
    }
    return false;
}

}
