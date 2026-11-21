#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ConPty_win.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <map>

namespace plynth::terminal {

namespace {

std::wstring lowerCase(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return text;
}

// Builds the environment block: the current environment with the given values replaced or added.
std::wstring makeEnvironmentBlock(const std::vector<std::pair<std::wstring, std::wstring>>& extra) {
    std::map<std::wstring, std::pair<std::wstring, std::wstring>> merged;
    wchar_t* strings = GetEnvironmentStringsW();
    if (strings) {
        for (const wchar_t* p = strings; *p; p += wcslen(p) + 1) {
            std::wstring entry = p;
            std::size_t eq = entry.find(L'=', 1);
            if (eq == std::wstring::npos) continue;
            std::wstring name = entry.substr(0, eq);
            merged[lowerCase(name)] = {name, entry.substr(eq + 1)};
        }
        FreeEnvironmentStringsW(strings);
    }
    for (const auto& item : extra) merged[lowerCase(item.first)] = item;
    std::wstring block;
    for (const auto& item : merged) {
        block += item.second.first + L"=" + item.second.second;
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

}  // namespace

struct ConPty::Impl {
    HPCON console = nullptr;
    HANDLE inputWrite = nullptr;
    HANDLE outputRead = nullptr;
    HANDLE process = nullptr;
    HANDLE job = nullptr;
};

ConPty::ConPty() : impl_(new Impl) {}

ConPty::~ConPty() {
    close();
    if (impl_->outputRead) CloseHandle(impl_->outputRead);
    if (impl_->process) CloseHandle(impl_->process);
    if (impl_->job) CloseHandle(impl_->job);
    delete impl_;
}

// Starts the program in a new pseudo console and puts it in a kill on close job.
bool ConPty::start(const ConPtyOptions& options, std::wstring* error) {
    auto fail = [&](const wchar_t* what) {
        DWORD code = GetLastError();
        if (error) *error = std::wstring(what) + L" (error " + std::to_wstring(code) + L")";
        close();
        return false;
    };
    if (impl_->job || impl_->console) {
        SetLastError(ERROR_ALREADY_EXISTS);
        return fail(L"already started");
    }

    HANDLE inputRead = nullptr;
    HANDLE outputWrite = nullptr;
    if (!CreatePipe(&inputRead, &impl_->inputWrite, nullptr, 0)) return fail(L"input pipe");
    if (!CreatePipe(&impl_->outputRead, &outputWrite, nullptr, 0)) {
        CloseHandle(inputRead);
        return fail(L"output pipe");
    }
    COORD size{options.columns, options.rows};
    HRESULT hr = CreatePseudoConsole(size, inputRead, outputWrite, 0, &impl_->console);
    CloseHandle(inputRead);
    CloseHandle(outputWrite);
    if (FAILED(hr)) {
        SetLastError(static_cast<DWORD>(hr));
        return fail(L"pseudo console");
    }

    impl_->job = CreateJobObjectW(nullptr, nullptr);
    if (!impl_->job) return fail(L"job object");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(impl_->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        return fail(L"job limits");
    }

    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuffer(attrSize);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuffer.data());
    if (!InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize)) return fail(L"attribute list");
    if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, impl_->console, sizeof(HPCON), nullptr, nullptr)) {
        DWORD code = GetLastError();
        DeleteProcThreadAttributeList(attrs);
        SetLastError(code);
        return fail(L"console attribute");
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attrs;
    // Without this a program can inherit this process's redirected standard handles instead of the console.
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = INVALID_HANDLE_VALUE;
    startup.StartupInfo.hStdOutput = INVALID_HANDLE_VALUE;
    startup.StartupInfo.hStdError = INVALID_HANDLE_VALUE;
    std::wstring command = options.commandLine;
    std::wstring env = makeEnvironmentBlock(options.environment);
    PROCESS_INFORMATION info{};
    BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
        env.data(), options.workingDir.empty() ? nullptr : options.workingDir.c_str(),
        &startup.StartupInfo, &info);
    DWORD createError = GetLastError();
    DeleteProcThreadAttributeList(attrs);
    if (!created) {
        SetLastError(createError);
        return fail(L"start program");
    }

    // Assigned while suspended so a child can never escape the job.
    if (!AssignProcessToJobObject(impl_->job, info.hProcess)) {
        DWORD code = GetLastError();
        TerminateProcess(info.hProcess, 1);
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
        SetLastError(code);
        return fail(L"assign to job");
    }
    ResumeThread(info.hThread);
    CloseHandle(info.hThread);
    impl_->process = info.hProcess;
    open_ = true;
    return true;
}

// Reads the next chunk of output from the console.
std::size_t ConPty::read(char* buffer, std::size_t size) {
    DWORD got = 0;
    if (!impl_->outputRead || !ReadFile(impl_->outputRead, buffer, static_cast<DWORD>(size), &got, nullptr)) return 0;
    return got;
}

// Sends keyboard bytes to the program.
bool ConPty::write(const char* data, std::size_t size) {
    if (!open_ || !impl_->inputWrite) return false;
    std::size_t done = 0;
    while (done < size) {
        DWORD wrote = 0;
        if (!WriteFile(impl_->inputWrite, data + done, static_cast<DWORD>(size - done), &wrote, nullptr)) return false;
        done += wrote;
    }
    return true;
}

// Tells the pseudo console its new size.
bool ConPty::resize(short columns, short rows) {
    if (!open_ || !impl_->console) return false;
    return SUCCEEDED(ResizePseudoConsole(impl_->console, COORD{columns, rows}));
}

// Counts the processes still alive in the job.
int ConPty::processCount() const {
    if (!open_ || !impl_->job) return 0;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
    if (!QueryInformationJobObject(impl_->job, JobObjectBasicAccountingInformation, &info, sizeof(info), nullptr)) return 0;
    return static_cast<int>(info.ActiveProcesses);
}

// Tells whether the first program has ended.
bool ConPty::hasExited() const {
    return !impl_->process || WaitForSingleObject(impl_->process, 0) == WAIT_OBJECT_0;
}

// Gives the exit code of the first program, or 259 while it still runs.
unsigned long ConPty::exitCode() const {
    DWORD code = STILL_ACTIVE;
    if (impl_->process) GetExitCodeProcess(impl_->process, &code);
    return code;
}

// Shuts down the programs and the console. The reading thread must keep draining until this returns.
void ConPty::close() {
    open_ = false;
    if (impl_->job) TerminateJobObject(impl_->job, 1);
    if (impl_->inputWrite) {
        CloseHandle(impl_->inputWrite);
        impl_->inputWrite = nullptr;
    }
    if (impl_->console) {
        ClosePseudoConsole(impl_->console);
        impl_->console = nullptr;
    }
    // Wakes a reader that is still waiting for output.
    if (impl_->outputRead) CancelIoEx(impl_->outputRead, nullptr);
}

}  // namespace plynth::terminal
