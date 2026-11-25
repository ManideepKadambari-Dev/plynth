#include "UpdateInternals.h"

#include "plynth/core/ProjectFiles.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace plynth::update_detail {

// The newer versions of a release list, highest first.
Status newerReleases(std::string_view listText, const Version& current, std::vector<Version>& out)
{
    out.clear();
    const nlohmann::json json = nlohmann::json::parse(listText, nullptr, false);
    if (json.is_discarded() || !json.is_array()) {
        return fail(ErrorCode::NetworkFailed, "The release server gave a release list that could not be read.");
    }
    const std::size_t count = std::min(json.size(), kMaxReleases);
    for (std::size_t i = 0; i < count; ++i) {
        const nlohmann::json& entry = json[i];
        if (!entry.is_object()) continue;
        if (const auto draft = entry.find("draft"); draft != entry.end() && (!draft->is_boolean() || draft->get<bool>())) {
            continue;
        }
        const auto tag = entry.find("tag_name");
        if (tag == entry.end() || !tag->is_string()) continue;
        const auto version = parseTag(tag->get<std::string>());
        if (version && *version > current) out.push_back(*version);
    }
    std::sort(out.begin(), out.end(), [](const Version& a, const Version& b) { return a > b; });
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return {};
}

}

namespace plynth {

// Asks GitHub for the releases and returns the newest one that may be installed.
UpdateCheck checkForUpdate(std::stop_token stop)
{
    using namespace update_detail;
    const UpdateEnvironment& env = environment();
    UpdateCheck result;
    const Installation installation = env.installation();
    result.current = installation.version;
    if (installation.kind != InstallKind::Installed) {
        result.status = fail(ErrorCode::UpdateNotInstalled, std::string(kNotInstalledMessage) + " " + std::string(kReleasesPage));
        return result;
    }
    if (env.keys.empty()) {
        result.status = fail(ErrorCode::UpdateNotAvailable, "This copy of Plynth has no update key built in, so it cannot update itself.");
        return result;
    }
    const auto current = parseVersion(installation.version);
    if (!current) {
        result.status = fail(ErrorCode::UpdateNotAvailable, "This copy of Plynth has no release version, so it cannot update itself.");
        return result;
    }
    UpdateStateFile state;
    if (Status s = readUpdateState(stateFile(), state); !s.ok()) {
        result.status = fail(s.code, s.message + " Delete " + pathToUtf8(stateFile()) + " to check again.", s.where);
        return result;
    }

    std::string list;
    HttpResult got = httpGetText({releasesListUrl(), "application/vnd.github+json", kMaxReleaseListBytes}, list, stop);
    if (!got.status.ok()) {
        result.status = got.status;
        return result;
    }
    std::vector<Version> versions;
    if (Status s = newerReleases(list, *current, versions); !s.ok()) {
        result.status = s;
        return result;
    }

    for (const Version& version : versions) {
        std::string text;
        got = httpGetText({releaseFileUrl(version, kSignatureName), "application/octet-stream",
                           static_cast<long long>(kMaxSignatureBytes)},
                          text, stop);
        if (!got.status.ok()) {
            if (got.status.code == ErrorCode::NetworkFailed && got.httpStatus == 404) continue;
            result.status = got.status;
            return result;
        }
        CheckedSignature checked;
        Status s = checkSignature(text, env.keys, checked);
        if (s.ok() && checked.version != version) {
            s = fail(ErrorCode::SignatureInvalid, "The signed version is " + versionText(checked.version) +
                                                      ", not the version of the release.");
        }
        if (!s.ok()) {
            result.warnings.push_back({"signature_invalid", "Version " + versionText(version) +
                                                                " was skipped because its signature is not valid. " + s.message});
            continue;
        }
        rememberSigned(stateFile(), version);
        if (state.highestSigned && version < *state.highestSigned) break;
        UpdateOffer offer;
        offer.version = versionText(version);
        offer.releaseUrl = releasePageUrl(version);
        offer.signature = text;
        offer.earlyPreview = version.major == 0;
        result.offer = std::move(offer);
        break;
    }
    return result;
}

// True only for an installed release, settings that could be read and the switch on.
bool shouldCheckAtStart(const Installation& installation, const SettingsLoad& settings)
{
    return installation.kind == InstallKind::Installed && settings.status.ok() && settings.settings.updateCheck;
}

}
