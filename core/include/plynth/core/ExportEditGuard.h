#pragma once

#include "EditSession.h"
#include "ExportGroups.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// Decides whether an edit of the export options may be written, from the files as they are at that moment.
//
// A ticket is what a menu, dialog, name editor or drag takes when it opens, with the ids it shows, and hands
// back when it writes. Showing the groups again ends a lock. It never makes an old ticket good again.
//
// A ticket is made useless by: a lock starting, the groups changing, another folder or export mode being
// shown, each saved group edit or repair, and show getting a project plan that gives other groups or labels
// to a list that follows the project's plan because building.blend has no built plan.
//
// The groups changed between two reviews of one grouped project when
//   the options file went from readable to unreadable or back, or
//   its saved group edits differ, or
//   the earlier review came from a build or build/building.blend was there when it was shown, and either an
//   id, name, member or rest of a group differs or a member's exportMemberLabel on its review's plan differs
//   (an id that was given to another thing).
// A change is always judged with the project that was last shown, so only the files can make one. Not a
// change: the kit switch, the file names, the notes, other files, the layout of the JSON, the project's own
// plan, the guard's own writes and anything in a single project.

// The decision about one edit.
enum class ExportEditDecision {
    Allowed,        // the guard let it through, and the status says whether it worked
    Locked,         // App: a command line session runs or ran since the ticket. Cli: the app has the project
    ChangedOutside, // the groups are not the ones the edit was made for
};

// Who writes, which decides what locks it.
enum class ExportEditor {
    App, // locked by an active session (readSession)
    Cli, // locked when allowCliCommand refuses, so its own session does not lock it
};

// What to say while a session locks the app.
inline constexpr std::string_view kExportSessionLockMessage =
    "The project is being edited from the command line. Editing is locked until it ends, and any open edit was "
    "not saved.";
// What to say from a change until the groups are shown again.
inline constexpr std::string_view kExportChangeLockMessage =
    "The groups changed. Editing is locked until the list reloads.";
// What to say after the reload when the saved group edits or the file changed.
inline constexpr std::string_view kExportGroupsChangedMessage =
    "The groups were changed outside the app. The list was reloaded and any open edit was not saved.";
// What to say after the reload when only a build changed the groups.
inline constexpr std::string_view kExportBuildChangedMessage =
    "A build changed the groups. The list was reloaded and any open edit was not saved.";

// Ties an edit to the groups it was started on. The default ticket is never good.
struct ExportEditTicket {
    std::uint64_t serial = 0;
};

// The answer to one write. When the decision is not Allowed the status is Locked and nothing was written.
struct ExportEditResult {
    ExportEditDecision decision = ExportEditDecision::Allowed;
    Status status;
};

// Changes the group edits, with the groups they give now to check against (renameExportGroup and the like).
using ExportGroupChange = std::function<Status(ExportGroupEdits&, const std::vector<ExportGroup>&)>;

// Guards every write of export-options.json made from one view or one command.
class ExportEditGuard {
public:
    // Makes a guard that has no project yet, so every write is refused.
    explicit ExportEditGuard(ExportEditor editor = ExportEditor::App);
    // Forgets the project.
    ~ExportEditGuard();

    ExportEditGuard(const ExportEditGuard&) = delete;
    ExportEditGuard& operator=(const ExportEditGuard&) = delete;

    // Takes what is about to be shown. Call it for every mode and for a bad options file too, with the review
    // just as reviewExportGroups gave it. It reads the session and works the review out again. False when
    // review is out of date by the rule of a change: do not show it, ask for a new one. Then it can only lock,
    // as folderChanged does. A current review ends a lock by a change, and ends a lock by a session that is
    // over or expired. An empty folder means no project.
    bool show(const std::filesystem::path& folder, const Project& project, const ExportGroupReview& review,
              SessionTime now);

    // Looks again after a folder event, and once a second while state() is not Allowed. It can lock but never
    // unlock. True when the caller should show a new review: something locked or changed, a session is over,
    // or the lock waits only for a reload.
    bool folderChanged(SessionTime now);

    // Gives the ticket for an edit that starts now. It is never good while locked or without a project.
    ExportEditTicket ticket() const;
    // Says whether an editor with this ticket may stay open.
    bool holds(ExportEditTicket ticket) const;

    // Writes one group edit, or refuses it. The order is: the session at now, the lock and the ticket, then
    // reviewExportGroups from the files as they are, compared with the last groups shown or written (a
    // difference locks), then change on the fresh options and groups. The session is read again right before
    // the write. A refusal of change writes nothing. Only an edit in grouped mode is taken.
    ExportEditResult edit(ExportEditTicket ticket, SessionTime now, const ExportGroupChange& change);

    // Writes the kit switch into the fresh options. It needs no ticket and only a session refuses it: a change
    // to the groups is kept, and is still found afterwards as a change.
    ExportEditResult setKit(SessionTime now, bool enabled);

    // Runs repairExportOptions after the same checks of session, lock and ticket, in every mode, and reads the
    // session again right before it. Only a file that was shown as unreadable and still is gets repaired.
    // Otherwise it is refused as ChangedOutside and nothing is written: nobody was asked about that file.
    ExportEditResult repair(ExportEditTicket ticket, SessionTime now);

    // Says whether editing is open or what locks it. A session comes before a change.
    ExportEditDecision state() const;

    // Gives what to show under the groups: the reason for the lock, otherwise the last change or refused edit
    // until a group edit or repair is saved or another folder or export mode is shown. It is empty when there
    // is none.
    std::string message() const;

private:
    struct State;
    std::unique_ptr<State> d_;
};

}
