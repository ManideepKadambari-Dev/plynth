#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <crtdbg.h>
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
