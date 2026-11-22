#pragma once

#include <plynth/core/ExportGroups.h>

#include <QString>
#include <QWidget>


#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>

class QLabel;
class QMenu;
class QPushButton;
class QTreeWidgetItem;

namespace plynth::app {

class GroupsTree;

// The list of export groups shown in the Export tab, with their members and edit actions. It writes the
// project's export options file and tells its owner to ask for the review again.
class ExportGroupsList : public QWidget {
    Q_OBJECT
public:
    explicit ExportGroupsList(QWidget* parent = nullptr);

    // Shows a review. With readOnly set, nothing can be edited. The plan, when given, gives members their labels.
    void setReview(const std::filesystem::path& projectFolder, const plynth::ExportGroupReview& review, bool readOnly,
                   const plynth::Plan* plan = nullptr);
    // True while a group name is being typed.
    bool editing() const;
    // True when a review that is about to be shown would lock the list: a session runs, or the groups differ from
    // the ones shown. The panel closes an open name editor first in that case.
    bool mustCloseEditor(bool readOnly, const plynth::ExportGroupReview* review) const;
    // Closes an open name editor without saving what was typed.
    void cancelEditing();
    // The project folder changed: lock now when the groups in the file are not the ones shown. A change that
    // leaves the groups alone, such as the kit switch, locks nothing.
    void checkOutside();
    // Opens every group (for screenshots) or closes them all.
    void setAllExpanded(bool expanded);

signals:
    // The export options file was written; ask for the review again.
    void edited();
    // The name editor was closed, by Enter, Escape or leaving the row.
    void editorClosed();
    // The groups were found to have changed outside the app; the list is locked, so ask for the review again.
    void outsideChanged();

private:
    void rebuild();
    void showMenu(const QPoint& at);
    void startRename(const std::string& group);
    void applyReview(const std::filesystem::path& projectFolder, const plynth::ExportGroupReview& review, bool readOnly,
                     const std::map<std::pair<std::string, std::string>, QString>& labels);
    bool locked() const { return readOnly_ || outsideLocked_; }
    void lockOutside();
    QString lockText() const;
    void renameTo(QTreeWidgetItem* item);
    void mergeGroup(const std::string& group, const std::string& into);
    void moveMember(const plynth::ExportMember& member, const std::string& to);
    void moveToNewGroup(const plynth::ExportMember& member);
    void newGroup();
    void resetGroups();
    bool freshOptions(plynth::ExportOptions& options, std::vector<plynth::ExportGroup>& current);
    bool store(const plynth::ExportOptions& options, const plynth::Status& status);
    void showMessage(const QString& text);
    QString newGroupPrompt() const;
    void addNote(QTreeWidgetItem* row, const QString& text);
    void updateButtons();

    std::filesystem::path folder_;
    plynth::ExportGroupReview review_;
    bool readOnly_ = false;
    bool outsideLocked_ = false;  // the groups changed outside the app and the list has not reloaded yet
    bool shownLocked_ = false;    // the locked state the tree was last built with
    std::vector<plynth::ExportGroup> expected_; // the groups the file should give now, as far as this list knows
    bool rebuilding_ = false;
    std::set<std::string> expanded_;
    std::map<std::pair<std::string, std::string>, QString> labels_;
    GroupsTree* tree_ = nullptr;
    QLabel* notes_ = nullptr;
    QLabel* empty_ = nullptr;
    QLabel* message_ = nullptr;
    QPushButton* newButton_ = nullptr;
    QPushButton* resetButton_ = nullptr;
};

} // namespace plynth::app
