#include "FakeBlender.h"
#include "SampleProject.h"
#include "SettingsGuard.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>

using plynth::cli::testing::isInsideFolder;
using plynth::cli::testing::ScopedEnvironment;
using plynth::cli::testing::testConfigFolder;
using plynth::cli::testing::utf8;

namespace {

// The value in the Win32 environment, or nothing when the variable is not there.
std::optional<std::wstring> win32Value(const wchar_t* name) {
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) {
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
            return std::nullopt;
        }
        return std::wstring();
    }
    std::wstring value(needed, L'\0');
    value.resize(GetEnvironmentVariableW(name, value.data(), needed));
    return value;
}

// The value in the C runtime copy of the environment, or nothing when the variable is not there.
std::optional<std::wstring> runtimeValue(const wchar_t* name) {
    wchar_t* text = nullptr;
    std::size_t size = 0;
    std::optional<std::wstring> value;
    if (_wdupenv_s(&text, &size, name) == 0 && text != nullptr) {
        value = std::wstring(text);
    }
    std::free(text);
    return value;
}

}

TEST_CASE("the test settings folder is set in both copies of the environment", "[cli][environment]") {
    const std::wstring expected = testConfigFolder().wstring();
    CHECK(win32Value(L"PLYNTH_CONFIG_DIR") == expected);
    CHECK(runtimeValue(L"PLYNTH_CONFIG_DIR") == expected);
}

TEST_CASE("a scoped environment on PLYNTH_CONFIG_DIR gives the test folder back", "[cli][environment]") {
    const std::wstring expected = testConfigFolder().wstring();
    const std::filesystem::path other = std::filesystem::path(L"C:/plynth-env-\u00e9\u00fc/cfg");
    {
        const ScopedEnvironment scoped("PLYNTH_CONFIG_DIR", utf8(other));
        CHECK(win32Value(L"PLYNTH_CONFIG_DIR") == other.wstring());
        CHECK(runtimeValue(L"PLYNTH_CONFIG_DIR") == other.wstring());
    }
    CHECK(win32Value(L"PLYNTH_CONFIG_DIR") == expected);
    CHECK(runtimeValue(L"PLYNTH_CONFIG_DIR") == expected);
}

TEST_CASE("a scoped environment restores a variable that only the Win32 copy had", "[cli][environment]") {
    const wchar_t* name = L"PLYNTH_TEST_WIN32_ONLY";
    SetEnvironmentVariableW(name, L"before \u00e9");
    REQUIRE_FALSE(runtimeValue(name).has_value());
    {
        const ScopedEnvironment scoped("PLYNTH_TEST_WIN32_ONLY", "inside");
        CHECK(win32Value(name) == std::wstring(L"inside"));
    }
    CHECK(win32Value(name) == std::wstring(L"before \u00e9"));
    CHECK_FALSE(runtimeValue(name).has_value());
    SetEnvironmentVariableW(name, nullptr);
}

TEST_CASE("a scoped environment removes a variable that was not there", "[cli][environment]") {
    {
        const ScopedEnvironment scoped("PLYNTH_TEST_NOT_SET", "inside");
        CHECK(win32Value(L"PLYNTH_TEST_NOT_SET") == std::wstring(L"inside"));
    }
    CHECK_FALSE(win32Value(L"PLYNTH_TEST_NOT_SET").has_value());
    CHECK_FALSE(runtimeValue(L"PLYNTH_TEST_NOT_SET").has_value());
}

TEST_CASE("the settings guard accepts only paths inside the temp folder", "[cli][environment]") {
    const std::filesystem::path temp = "C:/Users/someone/AppData/Local/Temp";
    CHECK(isInsideFolder("C:/Users/someone/AppData/Local/Temp/plynth-cli-config-1/settings.json", temp));
    CHECK(isInsideFolder("c:\\users\\SOMEONE\\appdata\\local\\temp\\x\\settings.json", temp));
    CHECK_FALSE(isInsideFolder("C:/Users/someone/AppData/Roaming/Plynth/settings.json", temp));
    CHECK_FALSE(isInsideFolder("C:/Users/someone/AppData/Local/Temp2/settings.json", temp));
    CHECK_FALSE(isInsideFolder("C:/Users/someone/AppData/Local/Temp/../../Roaming/Plynth/settings.json", temp));
    CHECK_FALSE(isInsideFolder("C:/Users/someone/AppData/Local/Temp", temp));
    CHECK_FALSE(isInsideFolder("", temp));
    CHECK_FALSE(isInsideFolder("settings.json", temp));
}
