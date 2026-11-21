#include "plynth/core/Document.h"

#include "plynth/core/ProjectFiles.h"

#include <system_error>
#include <utility>
#include <vector>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// Makes a folder path absolute and tidy, without a trailing separator.
fs::path normalizeFolder(const fs::path& folder)
{
    std::error_code ec;
    fs::path result = fs::absolute(folder, ec);
    if (ec) result = folder;
    result = result.lexically_normal();
    if (!result.has_filename() && result.has_parent_path() && result != result.root_path()) result = result.parent_path();
    return result;
}

// The refusal sent while the document is read only.
Status readOnlyStatus(const std::string& reason)
{
    return fail(ErrorCode::Locked, reason.empty() ? std::string("The project is read only.") : reason);
}

// Which content flags say how two versions of one floor differ.
unsigned floorDifference(const Floor& a, const Floor& b)
{
    unsigned flags = 0;
    if (a.image != b.image) flags |= Change::FloorImage;
    if (a.points != b.points) flags |= Change::Points;
    if (a.walls != b.walls) flags |= Change::Walls;
    if (a.rooms != b.rooms) flags |= Change::Rooms;
    if (a.openings != b.openings) flags |= Change::Openings;
    return flags;
}

// Which parts of the content of a floor exist at all, as content flags. Used for floors that were added or removed.
unsigned floorContent(const Floor& f)
{
    unsigned flags = 0;
    if (f.image) flags |= Change::FloorImage;
    if (!f.points.empty()) flags |= Change::Points;
    if (!f.walls.empty()) flags |= Change::Walls;
    if (!f.rooms.empty()) flags |= Change::Rooms;
    if (!f.openings.empty()) flags |= Change::Openings;
    return flags;
}

// Compares two projects and returns the Change flags of what differs. onlyFloor receives the id of the one
// floor that changed, or stays empty when several floors or none changed.
unsigned projectDifference(const Project& before, const Project& after, Id& onlyFloor)
{
    unsigned flags = 0;
    if (before.settings != after.settings) flags |= Change::Settings;
    if (before.plan.scale != after.plan.scale) flags |= Change::Scale;

    const auto& a = before.plan.floors;
    const auto& b = after.plan.floors;
    bool sameFloors = a.size() == b.size();
    for (std::size_t i = 0; sameFloors && i < a.size(); ++i) {
        sameFloors = a[i].id == b[i].id && a[i].name == b[i].name && a[i].ceilingHeight == b[i].ceilingHeight;
    }
    if (!sameFloors) flags |= Change::Floors;

    int changedFloors = 0;
    Id changedId;
    for (const Floor& old : a) {
        const Floor* now = findFloor(after.plan, old.id);
        const unsigned diff = now ? floorDifference(old, *now) : floorContent(old);
        if (diff != 0 || !now) {
            flags |= diff;
            ++changedFloors;
            changedId = old.id;
        }
    }
    for (const Floor& added : b) {
        if (findFloor(before.plan, added.id)) continue;
        flags |= floorContent(added);
        ++changedFloors;
        changedId = added.id;
    }
    if (changedFloors == 1 && sameFloors) onlyFloor = changedId;
    return flags;
}

// The image paths of every floor, in order.
std::vector<std::optional<std::string>> imagePaths(const Plan& plan)
{
    std::vector<std::optional<std::string>> images;
    for (const Floor& f : plan.floors) images.push_back(f.image);
    return images;
}

}

Document::Document() : project_(makeNewProject()) {}

const Project& Document::project() const
{
    return project_;
}

const Plan& Document::plan() const
{
    return project_.plan;
}

const fs::path& Document::folder() const
{
    return folder_;
}

bool Document::hasFolder() const
{
    return !folder_.empty();
}

// Swaps in another project and folder and forgets the undo history.
void Document::replace(Project project, fs::path folder, bool modified)
{
    project_ = std::move(project);
    folder_ = folder.empty() ? fs::path{} : normalizeFolder(folder);
    history_.clear();
    if (modified) history_.markNeverSaved();
    ++editCount_;
    autosavedEditCount_ = editCount_;
    autosaveWritten_ = false;
    readOnly_ = false;
    readOnlyReason_.clear();
    notify({Change::Everything, {}});
}

// Reads the project files again and takes them as the saved state when they differ from memory.
Status Document::reload()
{
    if (!hasFolder()) return fail(ErrorCode::InvalidValue, "This project has no folder yet, so there is nothing to reload.");
    ProjectLoad loaded = loadProject(folder_);
    if (!loaded.status.ok()) return loaded.status;
    if (loaded.project == project_) return {};

    DocumentEvent event;
    unsigned changes = projectDifference(project_, loaded.project, event.floorId);
    if (changes == 0) changes = Change::Content;
    event.changes = changes | Change::History;
    project_ = std::move(loaded.project);
    history_.clear();
    ++editCount_;
    notify(event);
    return {};
}

// Blocks or allows edits, for the edit lock.
void Document::setReadOnly(bool readOnly, std::string reason)
{
    readOnlyReason_ = readOnly ? std::move(reason) : std::string();
    if (readOnly == readOnly_) return;
    readOnly_ = readOnly;
    notify({Change::ReadOnly, {}});
}

// Says whether edits are blocked.
bool Document::isReadOnly() const
{
    return readOnly_;
}

// Opens a project folder and replaces the document with it.
Status Document::open(const fs::path& folder)
{
    ProjectLoad loaded = loadProject(folder);
    if (!loaded.status.ok()) return loaded.status;
    replace(std::move(loaded.project), folder, false);
    return {};
}

// Runs a command through the undo history and tells listeners.
Status Document::execute(std::unique_ptr<Command> command)
{
    if (readOnly_) return readOnlyStatus(readOnlyReason_);
    if (Status s = history_.push(std::move(command), project_); !s.ok()) return s;
    // After a merge the new command is gone, but the one it merged into is the last one and is on the same thing.
    const Command* last = history_.nextUndo();
    DocumentEvent event;
    event.changes = last->changes() | Change::History;
    event.floorId = last->floorId();
    ++editCount_;
    notify(event);
    return {};
}

bool Document::canUndo() const
{
    return !readOnly_ && history_.canUndo();
}

bool Document::canRedo() const
{
    return !readOnly_ && history_.canRedo();
}

// The name of the command undo would run.
std::string Document::undoName() const
{
    const Command* c = history_.nextUndo();
    return c ? c->name() : std::string();
}

// The name of the command redo would run.
std::string Document::redoName() const
{
    const Command* c = history_.nextRedo();
    return c ? c->name() : std::string();
}

// Undoes the last command and tells listeners.
void Document::undo()
{
    if (readOnly_) return;
    const Command* c = history_.nextUndo();
    if (!c) return;
    DocumentEvent event;
    event.changes = c->changes() | Change::History;
    event.floorId = c->floorId();
    history_.undo(project_);
    ++editCount_;
    notify(event);
}

// Applies the next undone command again and tells listeners.
Status Document::redo()
{
    if (readOnly_) return readOnlyStatus(readOnlyReason_);
    const Command* c = history_.nextRedo();
    if (!c) return {};
    DocumentEvent event;
    event.changes = c->changes() | Change::History;
    event.floorId = c->floorId();
    Status s = history_.redo(project_);
    if (!s.ok()) {
        notify({Change::History, {}});
        return s;
    }
    ++editCount_;
    notify(event);
    return s;
}

// Says whether there are unsaved changes.
bool Document::isModified() const
{
    return !history_.isAtSaved();
}

// Saves to the current folder and removes the autosave.
Status Document::save()
{
    if (readOnly_) return readOnlyStatus(readOnlyReason_);
    if (!hasFolder()) return fail(ErrorCode::InvalidValue, "This project has no folder yet. Use Save as.");
    const auto before = imagePaths(project_.plan);
    if (Status s = saveProject(project_, folder_, folder_); !s.ok()) return s;
    history_.markSaved();
    removeAutosave(folder_);
    autosaveWritten_ = false;
    DocumentEvent event;
    event.changes = Change::History;
    if (imagePaths(project_.plan) != before) event.changes |= Change::FloorImage;
    notify(event);
    return {};
}

// Saves to another folder and makes it the document's folder.
Status Document::saveAs(const fs::path& folder)
{
    if (readOnly_) return readOnlyStatus(readOnlyReason_);
    const fs::path target = normalizeFolder(folder);
    std::error_code ec;
    if (hasFolder() && fs::equivalent(target, folder_, ec)) return save();
    if (isProjectFolder(target)) {
        return fail(ErrorCode::AlreadyExists, "The folder " + pathToUtf8(target) + " already holds a project.");
    }
    Project copy = project_;
    if (Status s = saveProject(copy, target, folder_); !s.ok()) return s;

    DocumentEvent event;
    event.changes = Change::History | Change::Folder;
    if (imagePaths(copy.plan) != imagePaths(project_.plan)) event.changes |= Change::FloorImage;
    project_ = std::move(copy);
    folder_ = target;
    history_.markSaved();
    removeAutosave(folder_);
    autosaveWritten_ = false;
    ++editCount_;
    autosavedEditCount_ = editCount_;
    notify(event);
    return {};
}

// Says whether the last autosave lacks some changes.
bool Document::needsAutosave() const
{
    if (!hasFolder() || editCount_ == autosavedEditCount_) return false;
    return isModified() || autosaveWritten_;
}

// Writes the autosave, or removes a stale one when the document is back at its saved state.
Status Document::autosave()
{
    if (readOnly_ || !needsAutosave()) return {};
    if (!isModified()) {
        Status s = removeAutosave(folder_);
        if (s.ok()) {
            autosaveWritten_ = false;
            autosavedEditCount_ = editCount_;
        }
        return s;
    }
    Status s = writeAutosave(project_, folder_);
    if (s.ok()) {
        autosaveWritten_ = true;
        autosavedEditCount_ = editCount_;
    }
    return s;
}

// Removes the autosave.
Status Document::discardAutosave()
{
    if (!hasFolder()) return {};
    autosaveWritten_ = false;
    return removeAutosave(folder_);
}

// Adds a listener.
int Document::subscribe(DocumentListener listener)
{
    const int id = nextListenerId_++;
    listeners_[id] = std::move(listener);
    return id;
}

// Removes a listener.
void Document::unsubscribe(int listenerId)
{
    listeners_.erase(listenerId);
}

// Calls every listener with the event.
void Document::notify(const DocumentEvent& event)
{
    const auto listeners = listeners_;
    for (const auto& entry : listeners) {
        if (entry.second) entry.second(event);
    }
}

}
