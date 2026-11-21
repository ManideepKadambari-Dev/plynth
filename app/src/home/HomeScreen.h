#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace plynth::app {

class LibraryHost;

// The page shown when no project is open (DESIGN.md section 7.19e): recent projects with thumbnails on the
// right, and on the left the buttons to start or open a project, the help pages, the sample plans, the setup
// status and the projects folder. It asks the window to do things through signals and reads the recent list
// from the library host.
class HomeScreen : public QWidget {
    Q_OBJECT
public:
    explicit HomeScreen(LibraryHost& host, QWidget* parent = nullptr);

    // Builds the lists again from the settings, the setup check and the samples folder.
    void refresh();
    // Names the project that is open behind the home screen, or nothing. A button goes back to it.
    void setOpenProject(const QString& name);

signals:
    void newProjectRequested();
    void newFromImageRequested();
    void openProjectRequested();
    void openRecentRequested(const QString& folder);
    void openSampleRequested(const QString& imageFile);
    void setupRequested();
    void backRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QWidget* buildSide();
    QWidget* buildRecent();
    void fillRecent();
    void fillSide();
    QPushButton* addLink(QVBoxLayout* column, const QString& text, const QString& tip, const QString& tourId);

    LibraryHost& host_;
    QLineEdit* search_ = nullptr;
    QWidget* recentBody_ = nullptr;
    QVBoxLayout* recentColumn_ = nullptr;
    QPushButton* back_ = nullptr;
    QWidget* helpBox_ = nullptr;
    QVBoxLayout* helpColumn_ = nullptr;
    QWidget* samplesBox_ = nullptr;
    QVBoxLayout* samplesColumn_ = nullptr;
    QLabel* setupStatus_ = nullptr;
    QPushButton* setupFix_ = nullptr;
    QPushButton* projectsLink_ = nullptr;
    QLabel* versionLabel_ = nullptr;
};

}
