#pragma once

#include <QMainWindow>

namespace plynth::app {

class TitleBar;

// The main application window with its own title bar.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString& projectFolder, QWidget* parent = nullptr);

    QString projectName() const { return projectName_; }
    TitleBar* titleBar() const { return titleBar_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void toggleMaximized();
    void showAbout();

    QString projectName_;
    TitleBar* titleBar_ = nullptr;
};

}
