#pragma once

#include <plynth/core/Build.h>
#include <plynth/core/BuildCheck.h>

#include <QWidget>

#include <optional>

class QColor;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace plynth::app {

// The Build report window: the Check build button and what the last check found, then what the last build did,
// where its files are, how long each step took and its problems.
class BuildPanel : public QWidget {
    Q_OBJECT
public:
    explicit BuildPanel(QWidget* parent = nullptr);

    // Shows a report and a check. A null report shows the empty text, a null check shows none. stale says the plan
    // changed since that build, checkStale since that check.
    void setResults(const plynth::BuildReport* report, bool stale, const plynth::BuildCheck* check, bool checkStale);

signals:
    // A problem was clicked: the plan id of its object, empty when it has none.
    void problemActivated(const QString& objectId);
    void openFolderRequested();
    void openBlenderRequested();
    void checkRequested();

private:
    void rebuild();
    void addCheck(const plynth::BuildCheck& check);
    void addReport(const plynth::BuildReport& report);
    void addProblems(const std::vector<plynth::BuildProblem>& problems);
    QLabel* addNote(const QString& text, const QColor& color);
    QWidget* addSection(const QString& title);
    void addRow(QWidget* host, const QString& name, const QString& value, bool mono);

    QVBoxLayout* column_ = nullptr;
    QWidget* body_ = nullptr;
    QLabel* empty_ = nullptr;
    std::optional<plynth::BuildReport> report_;
    bool stale_ = false;
    std::optional<plynth::BuildCheck> check_;
    bool checkStale_ = false;
};

}
