#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <crtdbg.h>
#include <process.h>
#include <cstdlib>
#include <string>
#include <initializer_list>

namespace {

// Turns off every dialog a failing test program could show, so a failure prints and exits.
struct NoDialogs {
    NoDialogs() {
        // Settings never come from the real %APPDATA%: a folder under the temp folder is used.
        char* temp = nullptr;
        std::size_t size = 0;
        _dupenv_s(&temp, &size, "TEMP");
        const std::string config = std::string(temp ? temp : ".") + "\\plynth-tests-config";
        free(temp);
        _putenv_s("PLYNTH_CONFIG_DIR", config.c_str());
        // A store fetch never uses the real %LOCALAPPDATA%: it gets a short folder of this run, unless a test that
        // started this program named one.
        const std::string fetch = config.substr(0, config.size() - std::string("plynth-tests-config").size()) + "plynth-fetch-" + std::to_string(_getpid());
        char* named = nullptr;
        _dupenv_s(&named, &size, "PLYNTH_FETCH_FOLDER");
        if (!named) _putenv_s("PLYNTH_FETCH_FOLDER", fetch.c_str());
        free(named);
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
        for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
            _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
            _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
        }
#endif
    }
} noDialogs;

}  // namespace
