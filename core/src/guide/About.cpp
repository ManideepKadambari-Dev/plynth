#include "plynth/core/About.h"

#include "plynth/core/ProjectFiles.h"
#include "plynth/core/Version.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

// Fills in the fixed lines, then reads the notices and lists the license texts that are there.
AboutInfo aboutInfo(const fs::path& licensesFolder)
{
    AboutInfo info;
    info.version = std::string(version());
    info.copyright = std::string(kCopyrightLine);
    info.sourceLicense = std::string(kSourceLicense);
    info.binaryLicense = std::string(kBinaryLicense);
    info.sampleLicense = std::string(kSampleLicense);

    std::error_code ec;
    if (licensesFolder.empty() || !fs::is_directory(licensesFolder, ec)) {
        info.status = fail(ErrorCode::NotFound, "The licenses folder is missing, so the notices cannot be shown.");
        return info;
    }

    const fs::path notices = licensesFolder / pathFromUtf8(kNoticesFileName);
    if (fs::is_regular_file(notices, ec)) {
        std::ifstream in(notices, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        if (in && !in.bad()) {
            info.notices = buffer.str();
            info.noticesFile = notices;
        }
    }

    const fs::path texts = licensesFolder / pathFromUtf8(kLicenseTextsFolder);
    if (fs::is_directory(texts, ec)) {
        for (fs::directory_iterator it(texts, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec)) info.licenseFiles.push_back(it->path());
        }
        std::sort(info.licenseFiles.begin(), info.licenseFiles.end(),
                  [](const fs::path& a, const fs::path& b) { return a.filename() < b.filename(); });
    }

    if (info.noticesFile.empty()) {
        info.status = fail(ErrorCode::NotFound, "The file " + std::string(kNoticesFileName) + " is missing.");
    }
    return info;
}

}
