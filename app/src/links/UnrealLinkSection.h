#pragma once

#include <plynth/core/EngineLink.h>
#include <plynth/core/ExportEditGuard.h>
#include <plynth/core/Library.h>
#include <plynth/core/Project.h>
#include <plynth/core/Settings.h>

#include <QList>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace plynth::app {

class LinkRunner;

// What a send or a sync needs from the window: the project as it is in memory and the machine settings.
struct LinkContext {
    std::filesystem::path projectFolder;
    plynth::Project project;
    std::optional<plynth::LibraryPaths> library;
    plynth::MachineSettings settings;
};
using LinkContextSource = std::function<LinkContext()>;

// What the Export window tells the section each time it redraws.
struct UnrealSectionState {
    std::filesystem::path projectFolder; // empty when the project is not saved
    std::string projectName;
    bool projectGrouped = false; // the project's export mode, where the Grouped switch starts
    QString notice;              // why a send cannot start, such as no build; empty when it can
    bool running = false;        // a build or an export of the window runs
};

// The Unreal Engine section of the Export window (DESIGN.md section 7.16, owner 2026-11-27): whether the editor
// is open and with which project, the two choices Pack with materials and Grouped, the modular kit and gameplay
// data switches, Send to Unreal, Unlink and the last send. Its switches save through core; the search, the send
// and the sync after a build run off the window's thread.
class UnrealLinkSection : public QWidget {
    Q_OBJECT
public:
    UnrealLinkSection(plynth::ExportEditGuard& guard, QWidget* parent = nullptr);
    ~UnrealLinkSection() override;

    // Takes the project and the state of the window and redraws.
    void setState(const UnrealSectionState& state);
    // Greys every switch and button, while a command line session runs or the window shows an old review.
    void setLocked(bool locked);
    // Gives the section the window's project and settings for a send or a sync.
    void setContextSource(LinkContextSource source);
    // Looks for the editor now and every few seconds while on, so the status follows Unreal being opened or closed.
    void setWatching(bool on);
    // Looks for the editor once, unless a search runs.
    void lookForEditors();
    // Updates the linked Unreal project after a good build, when the building has been sent before.
    void syncAfterBuild();
    // Switches the modular kit or the gameplay data of the link, as the boxes do.
    void setKitEnabled(bool enabled);
    void setGameplayEnabled(bool enabled);
    // For screenshots: true once the first search after showing has ended.
    bool pictureReady() const;

signals:
    // A line for the window's status bar. refusal shows it in amber.
    void message(const QString& text, bool refusal);
    // A switch was saved or a send changed the link: the window reads the options again.
    void changed();

private:
    void rebuild();
    void clear();
    void addHeading();
    void addEditorStatus();
    void addTargetLine();
    void addSwitches();
    void addButtons();
    void addLastSend();
    QLabel* addLine(const QString& text, const QColor& color, bool smallText, const QString& tip);
    QWidget* addGrid(const QString& title);
    void addRow(QWidget* grid, const QString& name, const QString& value, bool mono);
    QPushButton* makeButton(QWidget* row, const QString& text, const QString& tip, bool primary);
    void updateEnabled();
    void searched(const plynth::EngineEditorSearch& search);
    void sent(const plynth::EngineLinkReport& report);
    void synced(const std::vector<plynth::EngineLinkReport>& reports);
    void send(bool force);
    void sendAnyway();
    void unlink();
    void saveChoice(bool packMaterials, bool on);
    void tookGuardResult(const plynth::ExportEditResult& result, const QString& what);
    void readOptions();
    void readReport();
    std::vector<plynth::EngineEditor> openEditors() const;
    std::optional<plynth::EngineEditor> chosenEditor() const;
    bool needsPicker() const;

    plynth::ExportEditGuard& guard_;
    LinkRunner* runner_ = nullptr;
    LinkContextSource context_;
    UnrealSectionState state_;
    plynth::EngineLinkOptions options_;
    QString optionsProblem_;             // why export-options.json cannot be read; empty when it can
    std::optional<plynth::EngineEditorSearch> search_;
    std::string chosenId_;               // the editor picked when several are open
    std::optional<plynth::EngineLinkReport> report_; // the last send, from this session or link_report.json
    std::optional<plynth::EngineLinkReport> fresh_;  // the last send of this session, with its quality checks
    QString progress_;
    QString error_;                      // why the last switch, send or unlink did not work
    QString syncLine_;                   // what the sync after the last build did
    bool syncRefusal_ = false;
    bool locked_ = false;
    bool watching_ = false;
    bool searchedOnce_ = false;
    QTimer* pollTimer_ = nullptr;
    QVBoxLayout* column_ = nullptr;
    QPointer<QLabel> progressLabel_;
    QList<QPointer<QWidget>> switches_;
    QPointer<QPushButton> sendButton_;
    QPointer<QPushButton> anywayButton_;
    QPointer<QPushButton> unlinkButton_;
    QPointer<QPushButton> stopButton_;
    QPointer<QComboBox> picker_;
};

}
