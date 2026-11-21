#pragma once

#include <QMainWindow>

namespace plynth::app {

// The main application window.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString& projectFolder, QWidget* parent = nullptr);

    QString projectName() const { return projectName_; }

private:
    QString projectName_;
};

}
