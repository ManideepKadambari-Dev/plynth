#include "guide/GuideHost.h"

#include "theme/Theme.h"
#include "window/MainWindow.h"

#include <GuideStyle.h>
#include <HelpWindow.h>
#include <TourController.h>
#include <WhatIsThis.h>

#include <plynth/core/Resources.h>

#include <QAbstractButton>
#include <QAction>
#include <QFile>
#include <QMenuBar>
#include <QPixmap>
#include <QRegularExpression>

namespace plynth::app {

namespace {

// The guide's colors, fonts and sizes, all from the theme file.
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
    s.dimAlpha = t.tourDimAlpha();
    s.uiFont = t.uiFont(t.sizeNormal());
    s.titleFont = t.uiFont(t.sizeLarge(), QFont::DemiBold);
    s.monoFont = t.monoFont(t.sizeNormal());
    s.unit = t.unit();
    s.radius = t.radius();
    s.cardWidth = t.tourCardWidth();
    s.outlineWidth = t.tourOutline();
    s.listWidth = t.helpListWidth();
    return s;
}

// Reads the tour text that is built into the program.
plynth::TourRead readBuiltInTour()
{
    QFile file(QStringLiteral(":/tour.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        plynth::TourRead read;
        read.status = plynth::fail(plynth::ErrorCode::CannotRead, "The tour text is missing from the program.");
        return read;
    }
    const QByteArray bytes = file.readAll();
    return plynth::readTour(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
}

// A folder from core as a path.
std::filesystem::path folderOf(plynth::ResourceKind kind)
{
    return plynth::resourceFolder(kind);
}

}

// Reads the tour text and makes the tour and What is this? helpers.
GuideHost::GuideHost(MainWindow* window) : QObject(window), window_(window)
{
    const plynth::TourRead read = readBuiltInTour();
    tourLoaded_ = read.status.ok();
    if (tourLoaded_) data_ = read.data;
    else tourProblem_ = QString::fromStdString(read.status.message + " " + read.status.where);
    const plynth::guide::GuideStyle style = styleFromTheme();
    tour_ = new plynth::guide::TourController(data_, this, style, this);
    whatIsThis_ = new plynth::guide::WhatIsThis(data_, this, style, this);
    connect(whatIsThis_, &plynth::guide::WhatIsThis::activeChanged, this, &GuideHost::whatIsThisChanged);
    connect(tour_, &plynth::guide::TourController::finished, this, &GuideHost::tourFinished);
}

GuideHost::~GuideHost() = default;

// Asks the window to show a screen by its tour name.
bool GuideHost::showScreen(const QString& screen)
{
    return window_->showScreenByName(screen);
}

// The tour name of the screen shown now.
QString GuideHost::currentScreen() const
{
    return window_->screenName();
}

// The window the tour draws over.
QWidget* GuideHost::window() const
{
    return window_;
}

// A visible widget with the id, or for a menu action the menu bar that opens its menu.
QWidget* GuideHost::findControl(const QString& id) const
{
    for (QWidget* widget : window_->findChildren<QWidget*>()) {
        if (widget->isVisible() && widget->property("tourId").toString() == id) return widget;
    }
    for (QAction* action : window_->findChildren<QAction*>()) {
        if (action->property("tourId").toString() != id) continue;
        for (QMenuBar* bar : window_->findChildren<QMenuBar*>()) {
            if (bar->isVisible()) return bar;
        }
    }
    return nullptr;
}

// The shortcut of an action or button, else the one named at the end of the tooltip, such as "(Ctrl+B)".
QString GuideHost::shortcutText(const QString& id) const
{
    for (QAction* action : window_->findChildren<QAction*>()) {
        if (action->property("tourId").toString() == id && !action->shortcut().isEmpty())
            return action->shortcut().toString(QKeySequence::NativeText);
    }
    QWidget* widget = findControl(id);
    if (auto* button = qobject_cast<QAbstractButton*>(widget); button && !button->shortcut().isEmpty())
        return button->shortcut().toString(QKeySequence::NativeText);
    if (!widget) return {};
    static const QRegularExpression key(QStringLiteral("\\(((?:Ctrl|Shift|Alt|F\\d+)[^()]{0,24}|[A-Z0-9\\[\\]])\\)\\s*$"));
    const QRegularExpressionMatch m = key.match(widget->toolTip());
    return m.hasMatch() ? m.captured(1) : QString();
}

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

// Ends What is this? and starts the tour.
bool GuideHost::startTour(const QString& tourId, int step)
{
    whatIsThis_->setActive(false);
    return tour_->start(tourId, step);
}

// Turns What is this? on or off, ending a tour first.
void GuideHost::setWhatIsThis(bool on)
{
    if (on) tour_->skip();
    whatIsThis_->setActive(on);
}

// Explains one control.
bool GuideHost::explain(const QString& controlId)
{
    tour_->skip();
    return whatIsThis_->explain(controlId);
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
