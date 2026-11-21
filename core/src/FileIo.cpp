#include "FileIo.h"

#include "plynth/core/ProjectFiles.h"

#include <chrono>
#include <fstream>
#include <system_error>
#include <thread>

namespace plynth::fileio {

namespace fs = std::filesystem;

// Writes text to path + ".tmp".
Status writeTemp(const fs::path& path, const std::string& text, fs::path& tmp)
{
    const std::string name = pathToUtf8(path.filename());
    tmp = path;
    tmp += ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return fail(ErrorCode::CannotWrite, name + " could not be written.");
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) {
        out.close();
        std::error_code ignore;
        fs::remove(tmp, ignore);
        return fail(ErrorCode::CannotWrite, name + " could not be written.");
    }
    return {};
}

// Renames tmp over target, trying again for a while when the target is in use.
Status renameOver(const fs::path& tmp, const fs::path& target)
{
    std::error_code ec;
    for (int attempt = 0; attempt < 20; ++attempt) {
        fs::rename(tmp, target, ec);
        if (!ec) return {};
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    std::error_code ignore;
    fs::remove(tmp, ignore);
    return fail(ErrorCode::CannotWrite, pathToUtf8(target.filename()) + " could not be replaced.");
}

// Writes text to a temporary file and renames it over the target.
Status writeAtomic(const fs::path& path, const std::string& text)
{
    fs::path tmp;
    if (Status s = writeTemp(path, text, tmp); !s.ok()) return s;
    return renameOver(tmp, path);
}

}
