#include "plynth/core/Status.h"

#include <array>
#include <utility>

namespace plynth {

namespace {

constexpr std::array kAllCodes = {
    ErrorCode::InvalidValue,  ErrorCode::NotFound,         ErrorCode::AlreadyExists,      ErrorCode::InUse,
    ErrorCode::NotAProject,   ErrorCode::CannotRead,       ErrorCode::CannotWrite,        ErrorCode::BadFile,
    ErrorCode::UnsupportedVersion, ErrorCode::ImageError,  ErrorCode::Locked,             ErrorCode::UnsavedChanges,
    ErrorCode::NoLibrary,     ErrorCode::BlenderNotFound,  ErrorCode::BlenderFailed,      ErrorCode::Cancelled,
    ErrorCode::DatabaseError, ErrorCode::QualityFailed,    ErrorCode::GraphicsUnavailable, ErrorCode::RenderFailed,
};

}

// Makes a failed status.
Status fail(ErrorCode code, std::string message, std::string where)
{
    Status s;
    s.code = code;
    s.message = std::move(message);
    s.where = std::move(where);
    return s;
}

// The stable lower case name of an error code.
std::string_view errorCodeName(ErrorCode code)
{
    switch (code) {
    case ErrorCode::None: return "ok";
    case ErrorCode::InvalidValue: return "invalid_value";
    case ErrorCode::NotFound: return "not_found";
    case ErrorCode::AlreadyExists: return "already_exists";
    case ErrorCode::InUse: return "in_use";
    case ErrorCode::NotAProject: return "not_a_project";
    case ErrorCode::CannotRead: return "cannot_read";
    case ErrorCode::CannotWrite: return "cannot_write";
    case ErrorCode::BadFile: return "bad_file";
    case ErrorCode::UnsupportedVersion: return "unsupported_version";
    case ErrorCode::ImageError: return "image_error";
    case ErrorCode::Locked: return "locked";
    case ErrorCode::UnsavedChanges: return "unsaved_changes";
    case ErrorCode::NoLibrary: return "no_library";
    case ErrorCode::BlenderNotFound: return "blender_not_found";
    case ErrorCode::BlenderFailed: return "blender_failed";
    case ErrorCode::Cancelled: return "cancelled";
    case ErrorCode::DatabaseError: return "database_error";
    case ErrorCode::QualityFailed: return "quality_failed";
    case ErrorCode::GraphicsUnavailable: return "graphics_unavailable";
    case ErrorCode::RenderFailed: return "render_failed";
    }
    return "invalid_value";
}

// One plain sentence saying when a code is given.
std::string_view errorCodeDescription(ErrorCode code)
{
    switch (code) {
    case ErrorCode::None: return "The command worked.";
    case ErrorCode::InvalidValue: return "A number or name is out of range or malformed.";
    case ErrorCode::NotFound: return "A floor, point, wall, room, opening, item, asset, material, build or file does not exist.";
    case ErrorCode::AlreadyExists: return "The thing to create is already there.";
    case ErrorCode::InUse: return "Something else uses it or is running on it, such as a build or an export.";
    case ErrorCode::NotAProject: return "The folder has no project.json.";
    case ErrorCode::CannotRead: return "A file or folder could not be opened.";
    case ErrorCode::CannotWrite: return "A file or folder could not be written.";
    case ErrorCode::BadFile: return "A file is not valid JSON or breaks its format rules.";
    case ErrorCode::UnsupportedVersion: return "The file was written by a newer Plynth.";
    case ErrorCode::ImageError: return "The plan image was refused by the image check.";
    case ErrorCode::Locked: return "The project is read only, or the app has it open and no session is active.";
    case ErrorCode::UnsavedChanges: return "The app has unsaved changes in this project.";
    case ErrorCode::NoLibrary: return "No library folder is set, or the folder is not a library.";
    case ErrorCode::BlenderNotFound: return "No Blender is set or found, or the one set is too old.";
    case ErrorCode::BlenderFailed: return "Blender ran but the job failed, crashed or ran out of time.";
    case ErrorCode::Cancelled: return "The user or the caller stopped the work.";
    case ErrorCode::DatabaseError: return "A catalog index could not be read or written.";
    case ErrorCode::QualityFailed: return "A quality check failed, so the export was not written.";
    case ErrorCode::GraphicsUnavailable: return "There is no graphics device, or the 3D view cannot start.";
    case ErrorCode::RenderFailed: return "The 3D view started but gave no usable picture.";
    }
    return "";
}

// Every error code once, in enum order.
std::span<const ErrorCode> allErrorCodes()
{
    return kAllCodes;
}

}
