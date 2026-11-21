#include "MainWindow.h"

#include <QDir>
#include <QFileInfo>
#include <QIcon>

namespace plynth::app {

MainWindow::MainWindow(const QString& projectFolder, QWidget* parent)
    : QMainWindow(parent)
{
    setWindowIcon(QIcon(QStringLiteral(":/logo.png")));
    if (projectFolder.isEmpty())
        projectName_ = tr("No project");
    else
        projectName_ = QFileInfo(QDir::cleanPath(projectFolder)).fileName();
    setWindowTitle(QStringLiteral("Plynth - ") + projectName_);
}

}
