#include "BuildBar.h"

#include "theme/PlainText.h"
#include "theme/TabButton.h"
#include "theme/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>

namespace plynth::app {

// Builds the tabs, the status area and the buttons from theme values.
BuildBar::BuildBar(QWidget* parent) : QWidget(parent)
{
    const Theme& t = Theme::instance();
    setFixedHeight(t.buildBarHeight());

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(t.unit() * 3, 0, t.unit() * 3, 0);
    row->setSpacing(t.unit() * 3);

    auto* tabs = new QWidget(this);
    tabs->setToolTip(tr("Switch between the plan and the 3D view of the building (3)"));
    auto* tabRow = new QHBoxLayout(tabs);
    tabRow->setContentsMargins(0, t.unit() * 2, 0, 0);
    tabRow->setSpacing(0);
    planTab_ = makeTabButton(tr("Plan"), tr("Show the plan"), tabs, t.unit() * 2,
                             t.unit() * 4);
    sceneTab_ = makeTabButton(tr("3D"), tr("Show the building in 3D (3)"), tabs,
                              t.unit() * 2, t.unit() * 4);
    tabRow->addWidget(planTab_);
    tabRow->addWidget(sceneTab_);
    row->addWidget(tabs);

    status_ = new QLabel(this);
    status_->setTextFormat(Qt::PlainText);
    status_->setFont(t.uiFont(t.sizeNormal()));
    status_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    status_->setToolTip(tr("What the last build did, or the progress of the one that is running"));
    // A long status is cut at the bar's edge instead of pushing the buttons out of the window.
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    row->addWidget(status_, 1);

    progress_ = new QProgressBar(this);
    progress_->setTextVisible(false);
    progress_->setRange(0, 100);
    progress_->setFixedWidth(t.unit() * 50);
    progress_->setFixedHeight(t.unit() * 2);
    progress_->setStyleSheet(
        QStringLiteral("QProgressBar { background: %1; border: none; border-radius: %3px; }"
                       "QProgressBar::chunk { background: %2; border-radius: %3px; }")
            .arg(t.line().name(), t.signalBlue().name())
            .arg(t.unit() / 2));
    progress_->setToolTip(tr("How far the build has come"));
    progress_->hide();
    row->addWidget(progress_);

    cancel_ = new QPushButton(tr("Cancel"), this);
    cancel_->setFocusPolicy(Qt::NoFocus);
    cancel_->setToolTip(tr("Stop the build. The last building stays as it was"));
    cancel_->hide();
    row->addWidget(cancel_);

    problems_ = new QPushButton(this);
    problems_->setFlat(true);
    problems_->setFocusPolicy(Qt::NoFocus);
    problems_->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    problems_->setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: none; color: %1; "
                                            "padding: %2px %3px; }"
                                            "QPushButton:hover { color: %4; }")
                                 .arg(t.signalAmber().name())
                                 .arg(t.unit())
                                 .arg(t.unit() * 2)
                                 .arg(t.text().name()));
    problems_->setToolTip(tr("Problems found by the last build. Click to list them"));
    problems_->hide();
    row->addWidget(problems_);

    build_ = new QPushButton(tr("Build"), this);
    build_->setFocusPolicy(Qt::NoFocus);
    build_->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    build_->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %1; "
                                         "border-radius: %3px; padding: %4px %5px; }"
                                         "QPushButton:disabled { background: %6; color: %7; border-color: %8; }")
                              .arg(t.signalBlue().name(), t.onBlue().name())
                              .arg(t.radius())
                              .arg(t.unit())
                              .arg(t.unit() * 5)
                              .arg(t.raised().name(), t.muted().name(), t.line().name()));
    build_->setToolTip(tr("Build the building in Blender (Ctrl+B)"));

    windows_ = new QWidget(this);
    auto* windowRow = new QHBoxLayout(windows_);
    windowRow->setContentsMargins(0, 0, 0, 0);
    windowRow->setSpacing(0);
    row->addWidget(windows_);
    row->addWidget(build_);

    panel_ = new QPushButton(tr("Properties"), this);
    panel_->setCheckable(true);
    panel_->setChecked(true);
    panel_->setFocusPolicy(Qt::NoFocus);
    panel_->setFont(t.uiFont(t.sizeNormal()));
    panel_->setToolTip(tr("Open or close the side panel with the properties of what is selected (Ctrl+Shift+P)"));
    panel_->setStyleSheet(QStringLiteral("QPushButton { background: transparent; color: %1; border: 1px solid %2; "
                                         "border-radius: %3px; padding: %4px %5px; }"
                                         "QPushButton:hover { color: %6; }"
                                         "QPushButton:checked { color: %6; border-color: %7; }")
                              .arg(t.muted().name(), t.line().name())
                              .arg(t.radius())
                              .arg(t.unit())
                              .arg(t.unit() * 3)
                              .arg(t.text().name(), t.signalBlue().name()));
    row->addWidget(panel_);

    connect(planTab_, &QPushButton::clicked, this, [this]() { emit viewRequested(View::Plan); });
    connect(sceneTab_, &QPushButton::clicked, this, [this]() { emit viewRequested(View::Scene3d); });
    connect(build_, &QPushButton::clicked, this, &BuildBar::buildRequested);
    connect(cancel_, &QPushButton::clicked, this, &BuildBar::cancelRequested);
    connect(problems_, &QPushButton::clicked, this, &BuildBar::problemsRequested);
    connect(panel_, &QPushButton::toggled, this, &BuildBar::panelToggled);
    setView(View::Plan);
    setStatus(tr("Not built yet"));
}

// Checks the tab that belongs to the view.
void BuildBar::setView(View view)
{
    view_ = view;
    planTab_->setChecked(view == View::Plan);
    sceneTab_->setChecked(view == View::Scene3d);
}

// Shows the status text alone.
void BuildBar::setStatus(const QString& text)
{
    status_->setText(text);
    progress_->hide();
    cancel_->hide();
}

// Shows the message with the progress bar and Cancel.
void BuildBar::setProgress(const QString& message, int percent)
{
    status_->setText(message);
    progress_->setRange(0, percent < 0 ? 0 : 100);
    progress_->setValue(percent < 0 ? 0 : percent);
    progress_->show();
    cancel_->show();
}

// Changes the Cancel button's tooltip.
void BuildBar::setCancelTip(const QString& tip)
{
    cancel_->setToolTip(plainToolTip(tip));
}

// Shows the problem count, or hides the button.
void BuildBar::setProblems(int errors, int warnings)
{
    const int total = errors + warnings;
    problems_->setVisible(total > 0);
    problems_->setText(total == 1 ? tr("1 problem") : tr("%1 problems").arg(total));
}

// Enables or disables the Build button.
void BuildBar::setBuildEnabled(bool enabled, const QString& tip)
{
    build_->setEnabled(enabled);
    build_->setToolTip(plainToolTip(tip));
}

// Adds a flat button for one of the app's windows, in the order the buttons are added.
void BuildBar::addWindowButton(const QString& name, const QString& text, const QString& tip)
{
    const Theme& t = Theme::instance();
    auto* button = new QPushButton(text, windows_);
    button->setFlat(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFont(t.uiFont(t.sizeNormal()));
    button->setToolTip(tip);
    button->setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: none; color: %1; "
                                         "padding: %2px %3px; }"
                                         "QPushButton:hover { color: %4; }")
                              .arg(t.muted().name())
                              .arg(t.unit())
                              .arg(t.unit() * 2)
                              .arg(t.text().name()));
    windows_->layout()->addWidget(button);
    connect(button, &QPushButton::clicked, this, [this, name]() { emit windowRequested(name); });
}

// Checks or clears the side panel button.
void BuildBar::setPanelOpen(bool open)
{
    const QSignalBlocker block(panel_);
    panel_->setChecked(open);
}

// Paints the Panel background with a Line border under the bar.
void BuildBar::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.fillRect(rect(), t.panel());
    p.fillRect(QRect(0, height() - 1, width(), 1), t.line());
}

}
