#pragma once

#include <string>
#include <string_view>

namespace plynth {

// Why an operation was refused. None means it worked.
// Each code has a stable lower case name (see errorCodeName) that the CLI prints in its answers.
enum class ErrorCode {
    None,
    InvalidValue,       // "invalid_value": a number or name is out of range or malformed
    NotFound,           // "not_found": a floor, point, wall, room, opening or file does not exist
    AlreadyExists,      // "already_exists": the thing to create is already there
    InUse,              // "in_use": removing it would leave something pointing at nothing
    NotAProject,        // "not_a_project": the folder has no project.json
    CannotRead,         // "cannot_read": a file or folder could not be opened
    CannotWrite,        // "cannot_write": a file or folder could not be written
    BadFile,            // "bad_file": a project file is not valid JSON or breaks the format rules
    UnsupportedVersion, // "unsupported_version": the file was written by a newer Plynth
    ImageError,         // "image_error": the plan image was refused by the image check
    Locked,             // "locked": the project is read only, or the app has it open and no session is active
    UnsavedChanges,     // "unsaved_changes": the app has unsaved changes, so a session cannot begin
};

// The outcome of an operation: ok, or an error code with one plain sentence saying why.
struct Status {
    ErrorCode code = ErrorCode::None;
    std::string message;
    // For file errors, a JSON pointer to the bad value, for example "/floors/0/walls/2/to". Empty otherwise.
    std::string where;

    bool ok() const { return code == ErrorCode::None; }
};

// Makes a failed status.
Status fail(ErrorCode code, std::string message, std::string where = {});

// The stable lower case name of an error code, for example "not_found". "ok" for None.
std::string_view errorCodeName(ErrorCode code);

}
