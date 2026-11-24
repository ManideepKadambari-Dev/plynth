#include "guide/GuideHost.h"

#include "theme/Theme.h"
#include "window/MainWindow.h"

#include <GuideStyle.h>
#include <HelpWindow.h>

#include <plynth/core/Resources.h>

#include <QPixmap>

namespace plynth::app {

namespace {

// The Help window's colors, fonts and sizes, all from the theme file.
plynth::guide::GuideStyle styleFromTheme()
{
    const Theme& t = Theme::instance();
    plynth::guide::GuideStyle s;
    s.ground = t.ground();
    s.panel = t.panel();
    s.raised = t.raised();
    s.line = t.line();
    s.text = t.text();
    s.muted = t.muted();
    s.blue = t.signalBlue();
    s.amber = t.signalAmber();
    s.onBlue = t.onBlue();
    s.uiFont = t.uiFont(t.sizeNormal());
    s.titleFont = t.uiFont(t.sizeLarge(), QFont::DemiBold);
    s.monoFont = t.monoFont(t.sizeNormal());
    s.unit = t.unit();
    s.radius = t.radius();
    s.listWidth = t.helpListWidth();
    return s;
}

// A folder from core as a path.
std::filesystem::path folderOf(plynth::ResourceKind kind)
{
    return plynth::resourceFolder(kind);
}

}

// Keeps the window the Help window belongs to.
GuideHost::GuideHost(MainWindow* window) : QObject(window), window_(window)
{
}

GuideHost::~GuideHost() = default;

// Makes the Help window the first time it is needed.
plynth::guide::HelpWindow* GuideHost::help()
{
    if (!help_) {
        auto* created = new plynth::guide::HelpWindow(styleFromTheme(), folderOf(plynth::ResourceKind::Docs),
                                                      folderOf(plynth::ResourceKind::Licenses),
                                                      QPixmap(QStringLiteral(":/logo.png")), window_);
        created->setWindowIcon(window_->windowIcon());
        created->setBackgroundRendering(!screenshotMode_);
        connect(created, &plynth::guide::HelpWindow::setupCheckRequested, this, &GuideHost::setupCheckRequested);
        help_ = created;
    }
    if (screenshotMode_) {
        help_->resize(window_->size());
        help_->move(window_->pos());
    } else if (!help_->isVisible()) {
        help_->resize(window_->size() * 3 / 4);
    }
    return help_;
}

// Shows a help page in the Help window.
void GuideHost::openHelp(const QString& page, const QString& anchor)
{
    plynth::guide::HelpWindow* window = help();
    window->showPage(page, anchor);
    window->show();
    window->raise();
    window->activateWindow();
}

// Shows search hits in the Help window.
void GuideHost::openSearch(const QString& text)
{
    plynth::guide::HelpWindow* window = help();
    window->showSearch(text);
    window->show();
    window->raise();
}

// Shows About in the Help window.
void GuideHost::openAbout()
{
    openHelp(QStringLiteral("about"));
}

// Says whether the Help window is on screen.
bool GuideHost::helpShown() const
{
    return help_ && help_->isVisible();
}

// A picture of the Help window.
QImage GuideHost::helpPicture() const
{
    return help_ ? help_->grab().toImage() : QImage();
}

}
