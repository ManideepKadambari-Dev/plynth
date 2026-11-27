#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;

namespace plynth::app {

// The Settings window. Its one section sets up the user's own coding assistant: a folder picker for the skills
// folder the assistant reads, then core's installSkill writes plynth/SKILL.md there (AgentFiles.h).
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    // Writes the skill file into this skills folder and shows the result. When the file is there already, it is
    // replaced only when askBeforeReplace is true and the user says yes. False when nothing was written.
    bool installInto(const QString& skillsFolder, bool askBeforeReplace);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void chooseSkillsFolder();
    bool askReplace(const QString& file);
    void showResult(const QString& text, bool refused);

    QPushButton* setUp_ = nullptr;
    QLabel* result_ = nullptr;
    QString lastFolder_;
};

}
