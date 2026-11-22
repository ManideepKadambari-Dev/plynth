#include "plynth/core/Status.h"

#include <utility>

namespace plynth {

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
    }
    return "invalid_value";
}

}
