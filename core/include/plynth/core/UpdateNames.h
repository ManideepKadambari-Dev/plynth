#pragma once

// Names shared by Plynth, its console launcher and its installer. Shipped clients depend on them, so they
// never change. PlynthRunning and PlynthSetup are session mutexes. Only programs with a package.json beside
// them (installed or staged) use them, so a build folder never blocks an installer.

namespace plynth {

inline constexpr wchar_t kRunningMutex[] = L"PlynthRunning";
inline constexpr wchar_t kSetupMutex[] = L"PlynthSetup";
inline constexpr wchar_t kPackageFile[] = L"package.json";

}
