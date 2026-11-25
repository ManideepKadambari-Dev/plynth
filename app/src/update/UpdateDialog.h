#pragma once

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;

namespace plynth::app {

class UpdateController;

// The update dialog: both versions, the size once known, the download progress and that Plynth closes for
// the installer. It is not modal, so editing goes on during the download. It only shows the controller's state
// and sends the buttons to it.
class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    explicit UpdateDialog(UpdateController& updates, QWidget* parent = nullptr);

private:
    void refresh();
    void primaryPressed();
    void secondaryPressed();
    void openReleases();

    UpdateController& updates_;
    QLabel* title_ = nullptr;
    QLabel* versions_ = nullptr;
    QLabel* body_ = nullptr;
    QLabel* warnings_ = nullptr;
    QProgressBar* bar_ = nullptr;
    QLabel* progress_ = nullptr;
    QLabel* note_ = nullptr;
    QLabel* problem_ = nullptr;
    QPushButton* releases_ = nullptr;
    QPushButton* secondary_ = nullptr;
    QPushButton* primary_ = nullptr;
};

}
