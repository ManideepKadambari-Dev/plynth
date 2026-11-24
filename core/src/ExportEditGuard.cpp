#include "plynth/core/ExportEditGuard.h"

#include "plynth/core/Build.h"
#include "plynth/core/BuildJob.h"
#include "plynth/core/ProjectFiles.h"

#include <system_error>
#include <utility>

namespace plynth {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kNoProjectText = "No project is shown, so nothing was saved.";
constexpr std::string_view kOldEditText = "This edit is out of date, so it was not saved.";
constexpr std::string_view kLockedSinceText = "Editing was locked after this edit was started, so it was not saved.";
constexpr std::string_view kSingleText = "This project exports one file, so its groups cannot be edited.";
constexpr std::string_view kNoRepairText =
    "The export options file is not the unreadable one that was shown, so it was not replaced.";

// What a person sees of one group, with the words for each of its members.
struct GroupRow {
    std::string id;
    std::string name;
    bool rest = false;
    std::vector<ExportMember> members;
    std::vector<std::string> labels;

    bool operator==(const GroupRow&) const = default;
};

// What has to stay the same for an edit to still mean what the person meant.
struct Snapshot {
    bool reviewable = false;
    bool readable = false;
    bool groupsCount = false; // false while nothing was built, when other groups are no change
    ExportGroupEdits edits;
    std::vector<GroupRow> rows;
};

// Keeps the part of a review that an edit depends on.
Snapshot snapshotOf(const fs::path& folder, const ExportGroupReview& review)
{
    Snapshot out;
    out.reviewable = review.reviewable;
    out.readable = review.status.ok();
    if (!out.reviewable || !out.readable) return out;

    std::error_code ignored;
    out.groupsCount =
        review.fromBuild || fs::exists(buildFolder(folder) / pathFromUtf8(kBuildBlendFileName), ignored);
    out.edits = review.options.groups;
    for (const ExportGroup& group : review.groups) {
        GroupRow row{group.id, group.name, group.rest, group.members, {}};
        for (const ExportMember& member : group.members) row.labels.push_back(exportMemberLabel(review.plan, member));
        out.rows.push_back(std::move(row));
    }
    return out;
}

// Says whether the groups changed from one look to the next.
bool changed(const Snapshot& earlier, const Snapshot& later)
{
    if (!earlier.reviewable || !later.reviewable) return false;
    if (earlier.readable != later.readable) return true;
    if (!later.readable) return false;
    return earlier.edits != later.edits || (earlier.groupsCount && earlier.rows != later.rows);
}

// Picks the sentence that says who changed the groups.
std::string_view reasonFor(const Snapshot& earlier, const Snapshot& later)
{
    const bool onlyBuild = earlier.readable == later.readable && earlier.edits == later.edits;
    return onlyBuild ? kExportBuildChangedMessage : kExportGroupsChangedMessage;
}

}

// What the guard remembers between two looks at the files.
struct ExportEditGuard::State {
    ExportEditor editor = ExportEditor::App;
    fs::path folder;
    Project project;
    Snapshot seen;
    bool shownUnreadable = false;

    bool sessionLock = false;
    bool changeLock = false;
    std::string sessionText;
    std::string notice;

    // A ticket is good while its number is this one.
    std::uint64_t round = 0;
    // A ticket with a lower number was taken before the last lock by a session.
    std::uint64_t sessionRound = 0;

    // Says what locks editing now, a session first.
    ExportEditDecision decision() const
    {
        if (sessionLock) return ExportEditDecision::Locked;
        return changeLock ? ExportEditDecision::ChangedOutside : ExportEditDecision::Allowed;
    }

    // Gives the words for what locks editing, or nothing.
    std::string lockText() const
    {
        if (sessionLock) return sessionText;
        return changeLock ? std::string(kExportChangeLockMessage) : std::string();
    }

    // Says whether an edit with this ticket may still be saved.
    bool holds(ExportEditTicket ticket) const
    {
        return !folder.empty() && decision() == ExportEditDecision::Allowed && ticket.serial == round;
    }

    // Reads the session and locks while one is in the way. True while one is.
    bool lookForSession(SessionTime now)
    {
        if (folder.empty()) return false;
        std::string why;
        if (editor == ExportEditor::App) {
            if (readSession(folder, now).state != SessionState::Active) return false;
            why = kExportSessionLockMessage;
        } else {
            const Status allowed = allowCliCommand(folder, now, false);
            if (allowed.ok()) return false;
            why = allowed.message;
        }
        sessionText = std::move(why);
        if (!sessionLock) {
            sessionLock = true;
            sessionRound = ++round;
        }
        return true;
    }

    // Locks when the files no longer give the groups that were shown. True when they do not.
    bool lookForChange(const ExportGroupReview& fresh)
    {
        const Snapshot later = snapshotOf(folder, fresh);
        if (!changed(seen, later)) return false;
        changeLock = true;
        notice = reasonFor(seen, later);
        ++round;
        return true;
    }

    // Refuses a write because editing is locked.
    ExportEditResult refuseLocked() const
    {
        return {decision(), fail(ErrorCode::Locked, lockText())};
    }

    // Refuses a write for another reason and keeps that reason to show.
    ExportEditResult refuse(ExportEditDecision why, std::string_view text)
    {
        notice = text;
        return {why, fail(ErrorCode::Locked, std::string(text))};
    }

    // Refuses an edit that was started before something happened, keeping what is said about that already.
    ExportEditResult refuseOld(ExportEditTicket ticket)
    {
        const bool session = ticket.serial != 0 && ticket.serial < sessionRound;
        const ExportEditDecision why = session ? ExportEditDecision::Locked : ExportEditDecision::ChangedOutside;
        const std::string text(folder.empty() ? kNoProjectText : session ? kLockedSinceText : kOldEditText);
        if (notice.empty()) notice = text;
        return {why, fail(ErrorCode::Locked, text)};
    }

    // Passes on how a write went. The reason of a failed one stays to be shown.
    ExportEditResult finished(Status status)
    {
        notice = status.message;
        return {ExportEditDecision::Allowed, std::move(status)};
    }
};

// Makes a guard that has no project yet.
ExportEditGuard::ExportEditGuard(ExportEditor editor) : d_(std::make_unique<State>())
{
    d_->editor = editor;
}

// Forgets the project.
ExportEditGuard::~ExportEditGuard() = default;

// Takes what is about to be shown, unless it is out of date.
bool ExportEditGuard::show(const fs::path& folder, const Project& project, const ExportGroupReview& review,
                           SessionTime now)
{
    State& d = *d_;
    if (folder.empty()) {
        d.folder.clear();
        d.seen = {};
        d.sessionLock = false;
        d.changeLock = false;
        d.notice.clear();
        ++d.round;
        return true;
    }

    const ExportGroupReview fresh = reviewExportGroups(folder, project);
    const Snapshot current = snapshotOf(folder, fresh);
    const Snapshot given = snapshotOf(folder, review);
    if (given.reviewable != current.reviewable || changed(given, current)) {
        folderChanged(now);
        return false;
    }

    if (folder != d.folder || project.settings.exportMode != d.project.settings.exportMode) {
        ++d.round;
        d.sessionLock = false;
        d.notice.clear();
    } else {
        // A change is looked for with the project that was shown, because the user's own plan edit is none.
        const Snapshot kept =
            project == d.project ? current : snapshotOf(folder, reviewExportGroups(folder, d.project));
        if (changed(d.seen, kept)) {
            d.notice = reasonFor(d.seen, kept);
            ++d.round;
        } else if (!fresh.fromBuild && current.groupsCount && current.rows != kept.rows) {
            ++d.round;
        }
    }

    d.folder = folder;
    d.project = project;
    d.seen = current;
    d.shownUnreadable = !review.status.ok();
    d.changeLock = false;
    if (!d.lookForSession(now)) d.sessionLock = false;
    return true;
}

// Looks at the files again. It can lock but never unlock.
bool ExportEditGuard::folderChanged(SessionTime now)
{
    State& d = *d_;
    if (d.folder.empty()) return false;
    const bool wasSessionLocked = d.sessionLock;
    const bool running = d.lookForSession(now);
    bool started = running && !wasSessionLocked;
    if (!d.changeLock && d.lookForChange(reviewExportGroups(d.folder, d.project))) started = true;
    return started || (!running && d.decision() != ExportEditDecision::Allowed);
}

// Gives the ticket for an edit that starts now.
ExportEditTicket ExportEditGuard::ticket() const
{
    if (d_->folder.empty() || d_->decision() != ExportEditDecision::Allowed) return {};
    return {d_->round};
}

// Says whether an editor with this ticket may stay open.
bool ExportEditGuard::holds(ExportEditTicket ticket) const
{
    return d_->holds(ticket);
}

// Writes one group edit, or refuses it.
ExportEditResult ExportEditGuard::edit(ExportEditTicket ticket, SessionTime now, const ExportGroupChange& change)
{
    State& d = *d_;
    d.lookForSession(now);
    if (d.decision() != ExportEditDecision::Allowed) return d.refuseLocked();
    if (!d.holds(ticket)) return d.refuseOld(ticket);
    if (!d.seen.reviewable) return d.refuse(ExportEditDecision::ChangedOutside, kSingleText);

    ExportGroupReview fresh = reviewExportGroups(d.folder, d.project);
    if (d.lookForChange(fresh)) return d.refuseLocked();
    if (!fresh.status.ok()) return d.finished(fresh.status);

    if (Status status = change(fresh.options.groups, fresh.groups); !status.ok()) return d.finished(std::move(status));
    if (d.lookForSession(now)) return d.refuseLocked();
    if (Status status = writeExportOptions(d.folder, fresh.options); !status.ok()) return d.finished(std::move(status));

    // Worked out from what was written, so a change that came right after the write is still found.
    fresh.groups = applyExportGroupEdits(fresh.automatic, fresh.options.groups, d.project.settings.name).groups;
    d.seen = snapshotOf(d.folder, fresh);
    ++d.round;
    return d.finished({});
}

// Writes the kit switch and keeps everything else in the file.
ExportEditResult ExportEditGuard::setKit(SessionTime now, bool enabled)
{
    State& d = *d_;
    if (d.folder.empty()) return d.refuse(ExportEditDecision::ChangedOutside, kNoProjectText);
    d.lookForSession(now);
    if (d.sessionLock) return d.refuseLocked();

    ExportOptionsRead read = readExportOptions(d.folder);
    if (!read.status.ok()) return {ExportEditDecision::Allowed, read.status};
    if (read.options.kit.enabled == enabled) return {};
    read.options.kit.enabled = enabled;
    if (d.lookForSession(now)) return d.refuseLocked();
    return {ExportEditDecision::Allowed, writeExportOptions(d.folder, read.options)};
}

// Writes the gameplay data switch into the fresh options, refused only by a session.
ExportEditResult ExportEditGuard::setGameplay(SessionTime now, bool enabled)
{
    State& d = *d_;
    if (d.folder.empty()) return d.refuse(ExportEditDecision::ChangedOutside, kNoProjectText);
    d.lookForSession(now);
    if (d.sessionLock) return d.refuseLocked();

    ExportOptionsRead read = readExportOptions(d.folder);
    if (!read.status.ok()) return {ExportEditDecision::Allowed, read.status};
    if (read.options.gameplay.enabled == enabled) return {};
    read.options.gameplay.enabled = enabled;
    if (d.lookForSession(now)) return d.refuseLocked();
    return {ExportEditDecision::Allowed, writeExportOptions(d.folder, read.options)};
}

// Turns the license report on or off, with the same rules as the gameplay switch.
ExportEditResult ExportEditGuard::setLicenses(SessionTime now, bool enabled)
{
    State& d = *d_;
    if (d.folder.empty()) return d.refuse(ExportEditDecision::ChangedOutside, kNoProjectText);
    d.lookForSession(now);
    if (d.sessionLock) return d.refuseLocked();

    ExportOptionsRead read = readExportOptions(d.folder);
    if (!read.status.ok()) return {ExportEditDecision::Allowed, read.status};
    if (read.options.licenses.enabled == enabled) return {};
    read.options.licenses.enabled = enabled;
    if (d.lookForSession(now)) return d.refuseLocked();
    return {ExportEditDecision::Allowed, writeExportOptions(d.folder, read.options)};
}

// Replaces the unreadable options file the person was asked about, or refuses.
ExportEditResult ExportEditGuard::repair(ExportEditTicket ticket, SessionTime now)
{
    State& d = *d_;
    d.lookForSession(now);
    if (d.decision() != ExportEditDecision::Allowed) return d.refuseLocked();
    if (!d.holds(ticket)) return d.refuseOld(ticket);

    const ExportGroupReview fresh = reviewExportGroups(d.folder, d.project);
    if (d.lookForChange(fresh)) return d.refuseLocked();
    if (!d.shownUnreadable || fresh.status.ok()) return d.refuse(ExportEditDecision::ChangedOutside, kNoRepairText);
    if (d.lookForSession(now)) return d.refuseLocked();

    ExportOptionsRepair repaired = repairExportOptions(d.folder);
    if (!repaired.status.ok()) return d.finished(std::move(repaired.status));
    if (!repaired.repaired) return d.refuse(ExportEditDecision::ChangedOutside, kNoRepairText);

    d.seen = snapshotOf(d.folder, reviewExportGroups(d.folder, d.project));
    d.shownUnreadable = false;
    ++d.round;
    return d.finished({});
}

// Says whether editing is open or what locks it.
ExportEditDecision ExportEditGuard::state() const
{
    return d_->decision();
}

// Gives the line to show under the groups.
std::string ExportEditGuard::message() const
{
    return d_->decision() == ExportEditDecision::Allowed ? d_->notice : d_->lockText();
}

}
