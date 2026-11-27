#pragma once

#include <plynth/core/Project.h>

#include <QDialog>

#include <filesystem>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace plynth::app {

// Build > Make variations: saves the project once per seed as <Stem>_v<seed> folders in an output folder, so a
// street of houses can be built from one plan. Lists the folders made, with Open folder.
class VariationsDialog : public QDialog {
    Q_OBJECT
public:
    VariationsDialog(const plynth::Project& project, const std::filesystem::path& projectFolder, QWidget* parent = nullptr);

    // For checks only: fills the seeds box.
    void setSeedsText(const QString& text);

private:
    void chooseFolder();
    void make();
    void updateButtons();

    const plynth::Project& project_;
    std::filesystem::path projectFolder_;
    QLineEdit* seeds_ = nullptr;
    QLineEdit* folder_ = nullptr;
    QPushButton* make_ = nullptr;
    QLabel* result_ = nullptr;
    QListWidget* made_ = nullptr;
    QPushButton* open_ = nullptr;
    bool seedsProblemShown_ = false;
};

}
