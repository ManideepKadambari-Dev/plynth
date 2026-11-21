#pragma once

#include "Command.h"
#include "CommandStack.h"
#include "Project.h"
#include "Status.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace plynth {

// What changed in a document, sent to every listener.
struct DocumentEvent {
    unsigned changes = 0; // Change flags
    Id floorId;           // the floor that changed, or empty when several or none did
};

using DocumentListener = std::function<void(const DocumentEvent& event)>;

// The open project: the single source of truth that every view and the CLI work on.
// It owns the project, the folder it belongs to and the undo history. The only way to change the
// project is execute, undo, redo, replace, or a save that copies images into plans/.
// Listeners are plain functions called right after each change, on the calling thread. A listener
// must not change the document from inside the call.
class Document {
public:
    // An untitled document holding makeNewProject(), with no folder.
    Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    const Project& project() const;
    const Plan& plan() const;

    // The project folder. Empty while the project has never been saved.
    const std::filesystem::path& folder() const;
    bool hasFolder() const;

    // Swaps in another project and folder, forgets the undo history and tells listeners Everything changed.
    // modified true marks it as having unsaved changes, as after recovering an autosave.
    void replace(Project project, std::filesystem::path folder, bool modified = false);

    // Opens a project folder with loadProject and replaces the document with it. On error the
    // document is unchanged.
    Status open(const std::filesystem::path& folder);

    // Runs a command through the undo history and tells listeners what it changed.
    // On error nothing changes and no listener is called.
    Status execute(std::unique_ptr<Command> command);

    bool canUndo() const;
    bool canRedo() const;

    // Names of the commands undo and redo would run, empty when there is none.
    std::string undoName() const;
    std::string redoName() const;

    void undo();
    Status redo();

    // Says whether there are changes that are not saved to the project folder.
    bool isModified() const;

    // Saves to the current folder with saveProject, then removes the autosave.
    // InvalidValue when the document has no folder yet: use saveAs.
    Status save();

    // Saves to another folder and makes it the document's folder. AlreadyExists when that folder holds
    // a different project. Images are copied from the old folder into the new one.
    Status saveAs(const std::filesystem::path& folder);

    // Says whether there are changes that the last autosave does not have. False without a folder.
    bool needsAutosave() const;

    // Writes the autosave with writeAutosave when needsAutosave is true.
    Status autosave();

    // Removes the autosave, for example when the user closes without saving.
    Status discardAutosave();

    // Adds a listener and returns a number to remove it with.
    int subscribe(DocumentListener listener);
    void unsubscribe(int listenerId);

private:
    void notify(const DocumentEvent& event);

    Project project_;
    std::filesystem::path folder_;
    CommandStack history_;
    std::map<int, DocumentListener> listeners_;
    int nextListenerId_ = 1;
    unsigned long long editCount_ = 0;
    unsigned long long autosavedEditCount_ = 0;
    bool autosaveWritten_ = false; // an autosave file from this session may exist
};

}
