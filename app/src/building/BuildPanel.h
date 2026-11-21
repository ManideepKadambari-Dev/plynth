#pragma once

#include <plynth/core/Build.h>

#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace plynth::app {

// The Build tab: what the last build did, where its files are, how long each step took and its problems.
class BuildPanel : public QWidget {
    Q_OBJECT
public:
    explicit BuildPanel(QWidget* parent = nullptr);

    // Shows a report. Null shows the empty text. stale says the plan changed since that build.
    void setReport(const plynth::BuildReport* report, bool stale);

signals:
    // A problem was clicked: the plan id of its object, empty when it has none.
    void problemActivated(const QString& objectId);
    void openFolderRequested();
    void openBlenderRequested();

private:
    QWidget* addSection(const QString& title);
    void addRow(QWidget* host, const QString& name, const QString& value, bool mono);

    QVBoxLayout* column_ = nullptr;
    QWidget* body_ = nullptr;
    QWidget* problemsHost_ = nullptr;
    QLabel* problemsHeading_ = nullptr;
    QVBoxLayout* problemColumn_ = nullptr;
    QWidget* problemBody_ = nullptr;
    QLabel* empty_ = nullptr;
};

}
