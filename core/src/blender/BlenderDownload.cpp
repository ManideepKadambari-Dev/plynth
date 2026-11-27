#include "plynth/core/BlenderDownload.h"

#include "BlenderDownloadSteps.h"
#include "BlenderPaths.h"
#include "update/Http.h"

#include "plynth/core/Process.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/Update.h"

#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <random>
#include <system_error>
#include <thread>
#include <vector>

namespace plynth {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kZipSuffix = "-windows-x64.zip";
constexpr std::string_view kStagingPrefix = ".download-";
constexpr std::string_view kStoppedMessage = "The Blender download was stopped. Nothing of it was kept.";
constexpr long long kMaxListingBytes = 4 * 1024 * 1024;
constexpr long long kMaxChecksumListBytes = 256 * 1024;
constexpr long long kProgressStep = 1024 * 1024;
constexpr std::chrono::minutes kUnpackTimeout{30};

// Says whether text is only ASCII digits and not empty.
bool allDigits(std::string_view text)
{
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// The text with spaces, tabs and line ends cut from both ends.
std::string_view trimmed(std::string_view text)
{
    const std::size_t start = text.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return {};
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

// The checksum as 64 lower case hex digits. Empty when the text is not 64 hex digits.
std::optional<std::string> hexDigest(std::string_view text)
{
    if (text.size() != 64) return std::nullopt;
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return std::nullopt;
    }
    return out;
}

// The file name an href points at: its last part, without "./" and with no query.
std::string_view linkedName(std::string_view href)
{
    const std::size_t cut = href.find_first_of("?#");
    if (cut != std::string_view::npos) href = href.substr(0, cut);
    const std::size_t slash = href.rfind('/');
    return slash == std::string_view::npos ? href : href.substr(slash + 1);
}

// Calls back with the target of every href in a page, in page order, with where the link ends.
template <typename Visit>
void forEachLink(std::string_view page, Visit visit)
{
    std::size_t at = 0;
    while ((at = page.find("href=", at)) != std::string_view::npos) {
        at += 5;
        if (at >= page.size()) break;
        const char quote = page[at];
        if (quote != '"' && quote != '\'') continue;
        const std::size_t end = page.find(quote, at + 1);
        if (end == std::string_view::npos) break;
        visit(page.substr(at + 1, end - at - 1), end + 1);
        at = end + 1;
    }
}

// Removes a folder and everything in it, trying again for a moment when a file is still held open, for
// example by a virus scanner looking at a new program.
void removeTree(const fs::path& folder)
{
    for (int attempt = 0; attempt < 20; ++attempt) {
        std::error_code ec;
        fs::remove_all(folder, ec);
        if (!fs::exists(folder, ec)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// Removes a staging folder when it goes out of scope.
struct StagingGuard {
    fs::path folder;
    ~StagingGuard()
    {
        if (!folder.empty()) removeTree(folder);
    }
};

// Sixteen random hex digits, for a staging folder's name.
std::string randomHex()
{
    std::random_device device;
    std::mt19937_64 engine(device());
    const unsigned long long value = engine();
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (int i = 15; i >= 0; --i) out.push_back(digits[(value >> (4 * i)) & 0xF]);
    return out;
}

// Removes staging folders left by a run that ended without cleaning up, such as one cut off by a power
// failure. Only folders older than a day, so a download running at the same time is never touched.
void removeStaleStaging(const fs::path& root)
{
    std::error_code ec;
    const auto now = fs::file_time_type::clock::now();
    std::vector<fs::path> stale;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        std::error_code entryEc;
        if (entry.is_symlink(entryEc) || !entry.is_directory(entryEc)) continue;
        if (pathToUtf8(entry.path().filename()).rfind(kStagingPrefix, 0) != 0) continue;
        const auto written = entry.last_write_time(entryEc);
        if (!entryEc && now - written > std::chrono::hours(24)) stale.push_back(entry.path());
    }
    for (const fs::path& folder : stale) removeTree(folder);
}

// Says whether a folder holds a finished download: blender.exe and the marker.
bool isFinishedCopy(const fs::path& folder)
{
    std::error_code ec;
    return fs::is_regular_file(folder / L"blender.exe", ec) &&
           fs::is_regular_file(folder / pathFromUtf8(kDownloadedBlenderMarker), ec);
}

// Says whether the drive of a folder has at least the given number of bytes free. True when it cannot tell.
bool hasRoom(const fs::path& folder, std::uint64_t bytes)
{
    std::error_code ec;
    const fs::space_info space = fs::space(folder, ec);
    return ec || space.available >= bytes;
}

// The status of a failed request, in the words of a Blender download.
Status requestFailed(const Status& status, std::string_view stopped)
{
    if (status.code == ErrorCode::Cancelled) return fail(ErrorCode::Cancelled, std::string(stopped));
    if (status.code == ErrorCode::CannotWrite) return status;
    return fail(ErrorCode::NetworkFailed, "Blender could not be downloaded from " + std::string(kBlenderDownloadHost) +
                                               ". " + status.message);
}

// Windows' own tar.exe in the system folder.
fs::path tarProgram()
{
    wchar_t folder[MAX_PATH];
    const UINT got = GetSystemDirectoryW(folder, MAX_PATH);
    if (got == 0 || got >= MAX_PATH) return {};
    return fs::path(std::wstring(folder, got)) / L"tar.exe";
}

// Inspects a downloaded copy and fills a download result from it.
BlenderDownload inspectedCopy(const fs::path& folder, std::stop_token stop)
{
    BlenderDownload out;
    BlenderInstall install = inspectBlender(folder / L"blender.exe", stop);
    install.sources = {BlenderSource::Downloaded};
    if (stop.stop_requested()) {
        out.status = fail(ErrorCode::Cancelled, std::string(kStoppedMessage));
        return out;
    }
    if (!install.usable()) {
        out.status = fail(ErrorCode::BlenderFailed, "The downloaded Blender in " + pathToUtf8(folder) + " does not run. " +
                                                        install.status.message);
        return out;
    }
    out.install = std::move(install);
    return out;
}

}

namespace blender_download {

// The tested series, from kTestedBlenderVersion.
std::string testedSeries()
{
    return std::to_string(kTestedBlenderVersion.major) + "." + std::to_string(kTestedBlenderVersion.minor);
}

// The release folder of a series.
std::string releaseFolderUrl(std::string_view series)
{
    return std::string(kBlenderReleaseFolder) + "Blender" + std::string(series) + "/";
}

// Reads the version from a Windows zip name of the series.
std::optional<BlenderVersion> zipVersion(std::string_view zipName, std::string_view series)
{
    const std::string prefix = "blender-" + std::string(series) + ".";
    if (zipName.size() <= prefix.size() + kZipSuffix.size()) return std::nullopt;
    if (zipName.substr(0, prefix.size()) != prefix) return std::nullopt;
    if (zipName.substr(zipName.size() - kZipSuffix.size()) != kZipSuffix) return std::nullopt;
    const std::string_view patch = zipName.substr(prefix.size(), zipName.size() - prefix.size() - kZipSuffix.size());
    if (!allDigits(patch) || patch.size() > 5 || (patch.size() > 1 && patch.front() == '0')) return std::nullopt;
    const auto version = parseBlenderVersion(std::string(series) + "." + std::string(patch));
    if (!version || formatBlenderVersion(*version) != std::string(series) + "." + std::string(patch)) return std::nullopt;
    return version;
}

// The checksum list name of a zip.
std::string checksumListName(std::string_view zipName)
{
    if (zipName.size() > kZipSuffix.size() && zipName.substr(zipName.size() - kZipSuffix.size()) == kZipSuffix) {
        return std::string(zipName.substr(0, zipName.size() - kZipSuffix.size())) + ".sha256";
    }
    return std::string(zipName) + ".sha256";
}

// The folder name of a zip's copy.
std::string copyFolderName(std::string_view zipName)
{
    if (zipName.size() > 4 && zipName.substr(zipName.size() - 4) == ".zip") return std::string(zipName.substr(0, zipName.size() - 4));
    return std::string(zipName);
}

// Reads the size the listing shows after a link to the file.
std::uint64_t listedSize(std::string_view page, std::string_view fileName)
{
    std::uint64_t size = 0;
    bool found = false;
    forEachLink(page, [&](std::string_view href, std::size_t after) {
        if (found || linkedName(href) != fileName) return;
        found = true;
        std::size_t lineEnd = page.find('\n', after);
        if (lineEnd == std::string_view::npos) lineEnd = page.size();
        std::string_view rest = page.substr(after, lineEnd - after);
        const std::size_t close = rest.rfind('>');
        if (close != std::string_view::npos) rest = rest.substr(close + 1);
        rest = trimmed(rest);
        const std::size_t space = rest.find_last_of(" \t");
        const std::string_view last = space == std::string_view::npos ? rest : rest.substr(space + 1);
        if (allDigits(last) && last.size() <= 15) size = std::stoull(std::string(last));
    });
    return size;
}

// Checks that an offer is one this code builds itself.
Status checkOffer(const BlenderOffer& offer)
{
    const std::string series = std::to_string(offer.version.major) + "." + std::to_string(offer.version.minor);
    const auto version = zipVersion(offer.zipName, series);
    const bool good = version && *version == offer.version && offer.zipUrl == releaseFolderUrl(series) + offer.zipName &&
                      hexDigest(offer.sha256) == offer.sha256;
    if (!good) return fail(ErrorCode::InvalidValue, "This is not a Blender download Plynth can use.");
    return {};
}

// Checks the names in the Blender zip and finds blender.exe.
Status checkBlenderZipEntries(const ZipListing& listing, std::string& exeFolder)
{
    exeFolder.clear();
    if (listing.entries.empty()) return fail(ErrorCode::BadFile, "The Blender download is an empty zip.");
    for (const ZipEntry& e : listing.entries) {
        const bool control = std::any_of(e.name.begin(), e.name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; });
        if (e.unsafe || control || e.name.empty()) {
            return fail(ErrorCode::BadFile, "The Blender download holds an unsafe file name, " + e.name + ", so nothing was unpacked.");
        }
        if (e.encrypted) return fail(ErrorCode::BadFile, "The Blender download holds an encrypted file, so nothing was unpacked.");
    }
    for (const ZipEntry& e : listing.entries) {
        if (e.folder) continue;
        if (e.name == "blender.exe") return {};
    }
    for (const ZipEntry& e : listing.entries) {
        if (e.folder) continue;
        const std::size_t slash = e.name.find('/');
        if (slash == std::string::npos || slash == 0) continue;
        if (e.name.substr(slash + 1) == "blender.exe") {
            exeFolder = e.name.substr(0, slash);
            if (exeFolder == ".") exeFolder.clear();
            return {};
        }
    }
    return fail(ErrorCode::BadFile, "The Blender download has no blender.exe in it.");
}

// Checks, unpacks, marks, moves and inspects a downloaded zip.
BlenderDownload installBlenderZip(const fs::path& root, const fs::path& staging, const BlenderOffer& offer,
                                  const ProgressFn& onProgress, std::stop_token stop)
{
    StagingGuard guard{staging};
    BlenderDownload out;
    const auto report = [&](BlenderDownloadStage stage, long long done, long long total) {
        if (onProgress) onProgress(BlenderDownloadProgress{stage, done, total});
    };
    const auto stopped = [&] {
        out.status = fail(ErrorCode::Cancelled, std::string(kStoppedMessage));
        return out;
    };
    const auto failed = [&](ErrorCode code, const std::string& message) {
        out.status = fail(code, message);
        return out;
    };
    if (Status s = checkOffer(offer); !s.ok()) {
        out.status = s;
        return out;
    }
    if (stop.stop_requested()) return stopped();
    const fs::path zip = staging / pathFromUtf8(offer.zipName);

    report(BlenderDownloadStage::Checking, 0, 0);
    const FileHash hash = sha256OfFile(zip, stop);
    if (hash.status.code == ErrorCode::Cancelled || stop.stop_requested()) return stopped();
    if (!hash.status.ok()) {
        out.status = hash.status;
        return out;
    }
    if (hash.sha256 != offer.sha256) {
        std::error_code ec;
        fs::remove(zip, ec);
        return failed(ErrorCode::ChecksumFailed, "The downloaded " + offer.zipName +
                                                     " does not match the checksum blender.org publishes for it, so it was deleted.");
    }

    const ZipListing listing = listZipEntries(zip);
    if (!listing.status.ok()) return failed(ErrorCode::BadFile, "The Blender download could not be read. " + listing.status.message);
    std::string exeFolder;
    if (Status s = checkBlenderZipEntries(listing, exeFolder); !s.ok()) {
        out.status = s;
        return out;
    }
    std::uint64_t unpackedBytes = 0;
    for (const ZipEntry& e : listing.entries) unpackedBytes += e.size;
    if (!hasRoom(staging, unpackedBytes + 64ull * 1024 * 1024)) {
        return failed(ErrorCode::CannotWrite, "There is not enough free space to unpack Blender in " + pathToUtf8(root) + ".");
    }
    if (stop.stop_requested()) return stopped();

    std::error_code ec;
    const fs::path unpackFolder = staging / L"unpacked";
    fs::create_directories(unpackFolder, ec);
    if (ec) return failed(ErrorCode::CannotWrite, "The folder " + pathToUtf8(unpackFolder) + " could not be created.");

    // Relative names only, run from the staging folder, so a folder name that is not ASCII never reaches tar.
    const long long total = static_cast<long long>(listing.entries.size());
    report(BlenderDownloadStage::Unpacking, 0, total);
    ProcessSpec spec;
    spec.program = tarProgram();
    spec.arguments = {"-x", "-v", "-f", offer.zipName, "-C", "unpacked"};
    spec.workingFolder = staging;
    spec.timeout = kUnpackTimeout;
    long long done = 0;
    const ProcessResult run = runProcess(spec, [&](std::string_view line) {
        if (line.rfind("x ", 0) != 0) return;
        ++done;
        if (done % 50 == 0) report(BlenderDownloadStage::Unpacking, std::min(done, total), total);
    }, stop);
    if (stop.stop_requested() || (run.status.code == ErrorCode::Cancelled && !run.timedOut)) return stopped();
    if (!run.status.ok() || run.exitCode != 0) {
        const std::string why = run.status.ok() ? "tar.exe ended with code " + std::to_string(run.exitCode) + "." : run.status.message;
        return failed(ErrorCode::CannotWrite, "Blender could not be unpacked in " + pathToUtf8(root) + ". " + why);
    }
    report(BlenderDownloadStage::Unpacking, total, total);

    const fs::path copy = exeFolder.empty() ? unpackFolder : unpackFolder / pathFromUtf8(exeFolder);
    if (!fs::is_regular_file(copy / L"blender.exe", ec)) {
        return failed(ErrorCode::BadFile, "The Blender download has no blender.exe after unpacking.");
    }
    Json marker = Json::object();
    marker["zip"] = offer.zipName;
    marker["url"] = offer.zipUrl;
    marker["sha256"] = offer.sha256;
    marker["version"] = formatBlenderVersion(offer.version);
    {
        std::ofstream file(copy / pathFromUtf8(kDownloadedBlenderMarker), std::ios::binary | std::ios::trunc);
        file << marker.dump(2) << "\n";
        file.close();
        if (!file) return failed(ErrorCode::CannotWrite, "The downloaded Blender could not be marked in " + pathToUtf8(copy) + ".");
    }
    if (stop.stop_requested()) return stopped();

    const fs::path target = root / pathFromUtf8(copyFolderName(offer.zipName));
    bool moved = false;
    if (isFinishedCopy(target)) {
        // Another download of the same version finished first. That copy is used as it is.
    } else if (fs::exists(target, ec)) {
        return failed(ErrorCode::CannotWrite, "The folder " + pathToUtf8(target) + " is in the way. Move it away and try again.");
    } else {
        fs::rename(copy, target, ec);
        if (ec) return failed(ErrorCode::CannotWrite, "The downloaded Blender could not be moved to " + pathToUtf8(target) + ".");
        moved = true;
    }

    report(BlenderDownloadStage::Inspecting, 0, 0);
    out = inspectedCopy(target, stop);
    if (moved && !out.status.ok()) removeTree(target);
    return out;
}

}

// The folder downloaded copies go into.
fs::path downloadedBlenderRoot()
{
    const Installation here = currentInstallation();
    if (here.kind == InstallKind::Installed && !here.folder.empty()) return here.folder / L"Blender";
    if (const std::wstring local = blenderpaths::environmentText(L"LOCALAPPDATA"); !local.empty()) {
        return fs::path(local) / L"Plynth" / L"Blender";
    }
    std::error_code ec;
    return fs::temp_directory_path(ec) / L"Plynth" / L"Blender";
}

// Picks the highest Windows zip of the series linked in a release folder page.
std::optional<std::string> pickBlenderZip(std::string_view page, std::string_view series)
{
    std::optional<std::string> best;
    BlenderVersion bestVersion;
    forEachLink(page, [&](std::string_view href, std::size_t) {
        const std::string_view name = linkedName(href);
        const auto version = blender_download::zipVersion(name, series);
        if (version && (!best || *version > bestVersion)) {
            best = std::string(name);
            bestVersion = *version;
        }
    });
    return best;
}

// Finds the checksum a sha256sum style list gives for a file name. Two different checksums for the same name
// give none, since then neither can be trusted.
std::optional<std::string> sha256For(std::string_view list, std::string_view fileName)
{
    std::optional<std::string> found;
    std::size_t start = 0;
    while (start <= list.size()) {
        std::size_t end = list.find('\n', start);
        if (end == std::string_view::npos) end = list.size();
        const std::string_view line = trimmed(list.substr(start, end - start));
        start = end + 1;
        const std::size_t gap = line.find_first_of(" \t");
        if (line.empty() || gap == std::string_view::npos) continue;
        std::string_view name = trimmed(line.substr(gap));
        if (!name.empty() && name.front() == '*') name.remove_prefix(1);
        if (name != fileName) continue;
        const auto digest = hexDigest(line.substr(0, gap));
        if (!digest || (found && *found != *digest)) return std::nullopt;
        found = digest;
    }
    return found;
}

// The name used in answers and progress.
std::string_view blenderDownloadStageName(BlenderDownloadStage stage)
{
    switch (stage) {
    case BlenderDownloadStage::Downloading: return "downloading";
    case BlenderDownloadStage::Checking: return "checking";
    case BlenderDownloadStage::Unpacking: return "unpacking";
    case BlenderDownloadStage::Inspecting: return "inspecting";
    }
    return "downloading";
}

// Reads the release folder of the tested series and its checksum list.
BlenderOfferCheck findBlenderOffer(std::stop_token stop)
{
    constexpr std::string_view stopped = "Looking for Blender on blender.org was stopped.";
    BlenderOfferCheck out;
    const std::string series = blender_download::testedSeries();
    const std::string folder = blender_download::releaseFolderUrl(series);

    update_detail::HttpRequest request;
    request.url = folder;
    request.accept = "text/html";
    request.maxBytes = kMaxListingBytes;
    request.allowedHosts = {std::string(kBlenderDownloadHost)};
    std::string page;
    if (const update_detail::HttpResult got = update_detail::httpGetText(request, page, stop); !got.status.ok()) {
        out.status = requestFailed(got.status, stopped);
        return out;
    }
    const auto zipName = pickBlenderZip(page, series);
    if (!zipName) {
        out.status = fail(ErrorCode::NetworkFailed, "blender.org lists no portable Blender " + series + " for Windows at " + folder + ".");
        return out;
    }

    request.url = folder + blender_download::checksumListName(*zipName);
    request.accept = "text/plain";
    request.maxBytes = kMaxChecksumListBytes;
    std::string list;
    if (const update_detail::HttpResult got = update_detail::httpGetText(request, list, stop); !got.status.ok()) {
        out.status = requestFailed(got.status, stopped);
        return out;
    }
    const auto sha = sha256For(list, *zipName);
    if (!sha) {
        out.status = fail(ErrorCode::NetworkFailed, "The checksum list on blender.org has no SHA-256 checksum for " + *zipName + ".");
        return out;
    }

    BlenderOffer offer;
    offer.version = *blender_download::zipVersion(*zipName, series);
    offer.zipName = *zipName;
    offer.zipUrl = folder + *zipName;
    offer.sha256 = *sha;
    offer.size = blender_download::listedSize(page, *zipName);
    out.offer = std::move(offer);
    return out;
}

// Downloads an offer into a staging folder of the download folder, then checks, unpacks and inspects it.
BlenderDownload downloadBlender(const BlenderOffer& offer, const std::function<void(const BlenderDownloadProgress&)>& onProgress,
                                std::stop_token stop)
{
    BlenderDownload out;
    const auto report = [&](BlenderDownloadStage stage, long long done, long long total) {
        if (onProgress) onProgress(BlenderDownloadProgress{stage, done, total});
    };
    if (Status s = blender_download::checkOffer(offer); !s.ok()) {
        out.status = s;
        return out;
    }
    const fs::path root = downloadedBlenderRoot();
    const fs::path target = root / pathFromUtf8(blender_download::copyFolderName(offer.zipName));
    if (isFinishedCopy(target)) {
        report(BlenderDownloadStage::Inspecting, 0, 0);
        return inspectedCopy(target, stop);
    }
    if (stop.stop_requested()) {
        out.status = fail(ErrorCode::Cancelled, std::string(kStoppedMessage));
        return out;
    }

    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec || !fs::is_directory(root, ec)) {
        out.status = fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(root) + " could not be created.");
        return out;
    }
    removeStaleStaging(root);
    // The zip and its unpacked copy, which is about three times the zip.
    if (offer.size > 0 && !hasRoom(root, offer.size * 4)) {
        out.status = fail(ErrorCode::CannotWrite, "There is not enough free space for Blender in " + pathToUtf8(root) + ".");
        return out;
    }
    const fs::path staging = root / pathFromUtf8(std::string(kStagingPrefix) + randomHex());
    if (!fs::create_directory(staging, ec) || ec) {
        out.status = fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(staging) + " could not be created.");
        return out;
    }
    StagingGuard guard{staging};

    const fs::path zip = staging / pathFromUtf8(offer.zipName);
    {
        std::ofstream file(zip, std::ios::binary | std::ios::trunc);
        if (!file) {
            out.status = fail(ErrorCode::CannotWrite, "The download could not be saved in " + pathToUtf8(staging) + ".");
            return out;
        }
        long long total = static_cast<long long>(offer.size);
        long long done = 0;
        long long shown = 0;
        report(BlenderDownloadStage::Downloading, 0, total);
        update_detail::HttpRequest request;
        request.url = offer.zipUrl;
        request.accept = "*/*";
        request.maxBytes = static_cast<long long>(kMaxBlenderZipBytes);
        request.allowedHosts = {std::string(kBlenderDownloadHost)};
        const update_detail::HttpResult got = update_detail::httpGet(
            request,
            [&](std::string_view piece) -> Status {
                file.write(piece.data(), static_cast<std::streamsize>(piece.size()));
                if (!file) {
                    return fail(ErrorCode::CannotWrite, "The download could not be saved in " + pathToUtf8(root) +
                                                            ". The disk may be full.");
                }
                done += static_cast<long long>(piece.size());
                if (done - shown >= kProgressStep) {
                    shown = done;
                    report(BlenderDownloadStage::Downloading, done, total);
                }
                return {};
            },
            [&](long long length) {
                if (length > 0) total = length;
            },
            stop);
        file.close();
        if (stop.stop_requested()) {
            out.status = fail(ErrorCode::Cancelled, std::string(kStoppedMessage));
            return out;
        }
        if (!got.status.ok()) {
            out.status = requestFailed(got.status, kStoppedMessage);
            return out;
        }
        if (!file) {
            out.status = fail(ErrorCode::CannotWrite, "The download could not be saved in " + pathToUtf8(root) + ". The disk may be full.");
            return out;
        }
        report(BlenderDownloadStage::Downloading, done, total > 0 ? total : done);
    }
    return blender_download::installBlenderZip(root, staging, offer, onProgress, stop);
}

// Hashes a file with SHA-256 through Windows' cryptography, a megabyte at a time.
FileHash sha256OfFile(const fs::path& file, std::stop_token stop)
{
    FileHash out;
    const auto cannotRead = [&] {
        out.status = fail(ErrorCode::CannotRead, "The file " + pathToUtf8(file) + " could not be read.");
        out.sha256.clear();
        return out;
    };
    std::ifstream in(file, std::ios::binary);
    if (!in) return cannotRead();

    struct Hasher {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        ~Hasher()
        {
            if (hash) BCryptDestroyHash(hash);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } h;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&h.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) ||
        !BCRYPT_SUCCESS(BCryptCreateHash(h.algorithm, &h.hash, nullptr, 0, nullptr, 0, 0))) {
        return cannotRead();
    }
    std::vector<char> buffer(1024 * 1024);
    while (in) {
        if (stop.stop_requested()) {
            out.status = fail(ErrorCode::Cancelled, "Checking the file was stopped.");
            return out;
        }
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got > 0 && !BCRYPT_SUCCESS(BCryptHashData(h.hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(got), 0))) {
            return cannotRead();
        }
    }
    if (in.bad()) return cannotRead();
    unsigned char digest[32];
    if (!BCRYPT_SUCCESS(BCryptFinishHash(h.hash, digest, static_cast<ULONG>(sizeof digest), 0))) return cannotRead();
    static constexpr char digits[] = "0123456789abcdef";
    for (const unsigned char byte : digest) {
        out.sha256.push_back(digits[byte >> 4]);
        out.sha256.push_back(digits[byte & 0xF]);
    }
    return out;
}

}
