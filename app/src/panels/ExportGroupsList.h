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
    // Takes the read only state at once, also while a name is being typed, so a commit that follows is refused.
    void setReadOnlyNow(bool readOnly) { readOnly_ = readOnly; }
    // Opens every group (for screenshots) or closes them all.
    void setAllExpanded(bool expanded);

signals:
    // The export options file was written; ask for the review again.
    void edited();
    // The name editor was closed, by Enter, Escape or leaving the row.
    void editorClosed();

private:
    void rebuild();
    void showMenu(const QPoint& at);
    void startRename(const std::string& group);
    void applyReview(const std::filesystem::path& projectFolder, const plynth::ExportGroupReview& review, bool readOnly,
                     const std::map<std::pair<std::string, std::string>, QString>& labels);
    void applyPending();
    void renameTo(QTreeWidgetItem* item);
    void mergeGroup(const std::string& group, const std::string& into);
    void moveMember(const plynth::ExportMember& member, const std::string& to);
    void moveToNewGroup(const plynth::ExportMember& member);
    void newGroup();
    void resetGroups();
    bool freshOptions(plynth::ExportOptions& options);
    bool store(const plynth::ExportOptions& options, const plynth::Status& status);
    void showMessage(const QString& text);
    QString newGroupPrompt() const;
    void addNote(QTreeWidgetItem* row, const QString& text);
    void updateButtons();

    std::filesystem::path folder_;
    plynth::ExportGroupReview review_;
    bool readOnly_ = false;
    bool shownReadOnly_ = false; // the read only state the tree was last built with
    bool rebuilding_ = false;
    std::set<std::string> expanded_;
    std::map<std::pair<std::string, std::string>, QString> labels_;
    // A review that arrived while a name was being typed; it is shown when the editor closes.
    struct Pending {
        std::filesystem::path folder;
        plynth::ExportGroupReview review;
        bool readOnly = false;
        std::map<std::pair<std::string, std::string>, QString> labels;
    };
    std::optional<Pending> pending_;
    GroupsTree* tree_ = nullptr;
    QLabel* notes_ = nullptr;
    QLabel* empty_ = nullptr;
    QLabel* message_ = nullptr;
    QPushButton* newButton_ = nullptr;
    QPushButton* resetButton_ = nullptr;
};

} // namespace plynth::app
