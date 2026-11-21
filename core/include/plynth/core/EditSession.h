#pragma once

#include "Status.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace plynth {

// The edit lock between the app and the CLI (DESIGN.md section 7.19c). Two small files in the project folder:
//   session.lock     written by "plynth session begin", removed by "plynth session end". While it is active
//                    the app is read only and redraws from plan.json.
//   app-state.json   written by the app while it has the project open, so the CLI knows whether the app has
//                    the project open and whether it has unsaved changes.
// Neither file is part of the project: saves, loads and Save as never read, write or copy them.
// Times in both files are UTC, ISO 8601 with whole seconds, for example "2026-11-21T10:00:00Z".

inline constexpr std::string_view kSessionFileName = "session.lock";
inline constexpr std::string_view kAppStateFileName = "app-state.json";

inline constexpr std::string_view kSessionFormat = "plynth-session";
inline constexpr int kSessionFormatVersion = 1;
inline constexpr std::string_view kAppStateFormat = "plynth-app-state";
inline constexpr int kAppStateFormatVersion = 1;

// A session with no CLI command for this long has expired and no longer locks the app.
inline constexpr int kSessionExpirySeconds = 300;

// The tool name written when session begin is given no --tool.
inline constexpr std::string_view kDefaultSessionTool = "plynth";

using SessionClock = std::chrono::system_clock;
using SessionTime = std::chrono::time_point<SessionClock, std::chrono::seconds>;

// The current time rounded down to whole seconds.
SessionTime sessionNow();

// Writes a time as "2026-11-21T10:00:00Z".
std::string formatSessionTime(SessionTime time);

// Reads a time written by formatSessionTime. Empty when the text is not in that form.
std::optional<SessionTime> parseSessionTime(std::string_view text);

// One run of a process. The start key tells a new process apart from an old one that had the same id
// (on Windows it is the process creation time). 0 when unknown.
struct ProcessIdentity {
    long long pid = 0;
    long long startKey = 0;

    bool operator==(const ProcessIdentity&) const = default;
};

// The identity of the process this code runs in.
ProcessIdentity currentProcess();

// Says whether that exact run of a process is still going. A start key of 0 only checks the id.
bool isProcessRunning(const ProcessIdentity& process);

// What session.lock holds.
struct SessionLock {
    long long pid = 0;   // the process that ran session begin, shown in session status only
    std::string tool;    // who is editing, from session begin --tool, shown in the app banner
    SessionTime started{};
    SessionTime lastActivity{};

    bool operator==(const SessionLock&) const = default;
};

enum class SessionState {
    None,    // no session.lock
    Active,  // a valid lock with recent activity: the app is read only
    Expired, // a lock that is too old, or a file that cannot be read: treated like no lock
};

// The stable lower case name of a session state: "none", "active" or "expired".
std::string_view sessionStateName(SessionState state);

// The answer from reading session.lock.
struct SessionRead {
    SessionState state = SessionState::None;
    SessionLock lock;          // filled when the file could be read
    SessionTime expiresAt{};   // lastActivity plus kSessionExpirySeconds, when the file could be read
    Status status;             // BadFile or CannotRead when the file exists but could not be used
};

// Reads folder/session.lock and decides its state at time now. A lock whose last activity is more than
// kSessionExpirySeconds before now, or more than that far after now (the clock was changed), is Expired.
SessionRead readSession(const std::filesystem::path& folder, SessionTime now);

// Writes folder/session.lock through a temporary file and a rename.
Status writeSession(const std::filesystem::path& folder, const SessionLock& lock);

// Deletes folder/session.lock. Ok when there is none.
Status removeSession(const std::filesystem::path& folder);

// What app-state.json holds.
struct AppState {
    ProcessIdentity process; // the app that has the project open
    bool modified = false;   // the app has unsaved changes
    SessionTime updated{};

    bool operator==(const AppState&) const = default;
};

// The answer from reading app-state.json.
struct AppStateRead {
    bool open = false; // the file exists, is valid and its process is still running
    AppState state;    // filled when the file could be read
    Status status;     // BadFile or CannotRead when the file exists but could not be used
};

// Reads folder/app-state.json. A file whose process is no longer running, or that cannot be read,
// gives open false, so a crashed app never blocks the CLI.
AppStateRead readAppState(const std::filesystem::path& folder);

// Writes folder/app-state.json through a temporary file and a rename.
Status writeAppState(const std::filesystem::path& folder, const AppState& state);

// Deletes folder/app-state.json, but only when it belongs to the given process, so one app never removes
// another app's file. Ok when there is none.
Status removeAppState(const std::filesystem::path& folder, const ProcessIdentity& owner);

// Starts or continues a session for "plynth session begin". Refuses with NotAProject when the folder holds
// no project and with UnsavedChanges when the app has the project open with unsaved changes. An active
// session keeps its start time and gets a new last activity; otherwise a new lock is written.
// out, when given, receives the lock as written; alreadyActive says whether a session was running.
Status beginSession(const std::filesystem::path& folder, std::string_view tool, long long pid, SessionTime now,
                    SessionLock* out = nullptr, bool* alreadyActive = nullptr);

// Ends the session for "plynth session end" by removing session.lock, whatever its state.
// wasActive, when given, says whether an active session was ended.
Status endSession(const std::filesystem::path& folder, SessionTime now, bool* wasActive = nullptr);

// Called by every CLI command that opens a project, before it changes anything.
// With an active session: moves its last activity to now and returns Ok.
// Without one: Ok when the app does not have the project open, otherwise Locked with a sentence telling
// the caller to run "plynth session begin" first (or again, when the session expired).
// readOnly true is for commands that only read, such as project info: they are never refused, but still
// refresh an active session.
Status allowCliCommand(const std::filesystem::path& folder, SessionTime now, bool readOnly);

}
