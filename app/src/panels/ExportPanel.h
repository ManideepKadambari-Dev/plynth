#pragma once

#include <plynth/core/Export.h>

#include <QHash>
#include <QString>
#include <QWidget>

class QPushButton;
class QVBoxLayout;

namespace plynth::app {

// What the Export tab needs to know besides the report.
struct ExportPanelState {
    bool running = false;      // an export or a build is running, so the buttons wait
    bool canRun = false;       // there is a build, the project is not grouped and setup is fine
    QString notice;            // why it cannot run, shown above the report; empty when it can
    bool offerBuild = false;   // show the Build button beside the notice
    QString error;             // the sentence of the last export that failed before it had a report
    bool folderExists = false; // the export folder is there to open
};

// The Export tab: the Export and Check buttons, the last export and the list of quality checks.
class ExportPanel : public QWidget {
    Q_OBJECT
public:
    explicit ExportPanel(QWidget* parent = nullptr);

    // Shows a report, or the empty text when it is null.
    void setReport(const plynth::ExportReport* report, const ExportPanelState& state);

signals:
    void exportRequested();
    void checkRequested();
    void exportAnywayRequested();
    void buildRequested();
    void openFolderRequested();
    // A finding was clicked: the plan id of its object.
    void findingActivated(const QString& planId);

private:
    QWidget* addSection(const QString& title);
    void addRow(QWidget* host, const QString& name, const QString& value, bool mono);
    QPushButton* addButton(QWidget* row, const QString& text, const QString& tip, const QString& tourId, bool primary);
    void addChecks(const plynth::ExportReport& report);

    QVBoxLayout* column_ = nullptr;
    QWidget* body_ = nullptr;
    QHash<QString, bool> expanded_;
};

}
