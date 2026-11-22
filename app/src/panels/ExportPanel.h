#pragma once

#include <plynth/core/Export.h>
#include <plynth/core/ExportGroups.h>

#include <QHash>
#include <QString>
#include <QWidget>

#include <filesystem>
#include <optional>

class QFileSystemWatcher;
class QPushButton;
class QShowEvent;
class QTimer;
class QTreeWidget;
class QVBoxLayout;

namespace plynth::app {

class ExportGroupsList;

// What the Export tab needs to know besides the report.
struct ExportPanelState {
    bool running = false;      // an export or a build is running, so the buttons wait
    bool canRun = false;       // there is a build and setup is fine
    QString notice;            // why it cannot run, shown above the report; empty when it can
    bool offerBuild = false;   // show the Build button beside the notice
    QString error;             // the sentence of the last export that failed before it had a report
    bool folderExists = false; // the export folder is there to open
    bool grouped = false;      // the project exports one FBX for each group
    bool built = false;        // the project has a build, so its groups can be shown
    QString optionsProblem;    // why export-options.json cannot be read; empty when it can
    bool optionsRepairable = false; // the bad file may be replaced; false for one written by a newer Plynth
    bool kitEnabled = false;   // export-options.json switches the modular kit on
    bool readOnly = false;     // a CLI session holds the project, so the groups cannot be edited
    std::filesystem::path projectFolder;               // empty when the project is not saved
    const plynth::ExportGroupReview* review = nullptr; // copied by setReport; null without a project folder
};

// The Export tab: the Export and Check buttons, the last export and the list of quality checks.
class ExportPanel : public QWidget {
    Q_OBJECT
public:
    explicit ExportPanel(QWidget* parent = nullptr);
    ~ExportPanel() override;

    // Shows a report, or the empty text when it is null.
    void setReport(const plynth::ExportReport* report, const ExportPanelState& state);
    // For checks only: opens every group of the list, now and after each refresh.
    void setGroupsExpanded(bool expanded);
    // For checks only: switches the modular kit on in the project's export options, as the checkbox does.
    void setKitEnabled(bool enabled);

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void exportRequested();
    void checkRequested();
    void exportAnywayRequested();
    void buildRequested();
    void openFolderRequested();
    // A finding was clicked: the plan id of its object.
    void findingActivated(const QString& planId);
    // The groups were edited, an options file appeared or changed, or the tab was shown: ask for a new review.
    void refreshRequested();

private:
    QWidget* addSection(const QString& title);
    void addRow(QWidget* host, const QString& name, const QString& value, bool mono);
    QPushButton* addButton(QWidget* row, const QString& text, const QString& tip, const QString& tourId, bool primary);
    void addChecks(const plynth::ExportReport& report);
    void addGroups(const ExportPanelState& state);
    void addKitSwitch(const ExportPanelState& state);
    void applyDeferred();
    void addOptionsProblem(const ExportPanelState& state);
    void resetOptions();
    void addKitReport(const plynth::ExportReport& report);
    void fitGroupsTree();
    void fitGroupsColumns();
    void watchFolder(const std::filesystem::path& folder);

    QVBoxLayout* column_ = nullptr;
    QWidget* body_ = nullptr;
    QHash<QString, bool> expanded_;
    ExportGroupsList* groups_ = nullptr;
    // What setReport was given while a group name was being typed; shown when the editor closes.
    struct Deferred {
        std::optional<plynth::ExportReport> report;
        ExportPanelState state;
        plynth::ExportGroupReview review;
    };
    std::optional<Deferred> deferred_;
    bool deferredHasReview_ = false;
    QTreeWidget* groupsTree_ = nullptr;
    plynth::ExportGroupReview review_;
    bool groupsExpanded_ = false;
    bool groupsExpandedDone_ = false;
    QFileSystemWatcher* watcher_ = nullptr;
    QTimer* watchTimer_ = nullptr;
    std::filesystem::path watched_;
    std::filesystem::path projectFolder_;
    QString resetError_; // why the options file could not be removed; empty when it could
    QString kitError_; // why the last change of the kit switch was not saved; empty when it was
};

}
