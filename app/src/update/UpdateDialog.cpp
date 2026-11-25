#include "UpdateDialog.h"

#include "UpdateController.h"
#include "theme/Theme.h"

#include <plynth/core/Update.h>

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace plynth::app {

namespace {

constexpr int kDialogWidth = 560;
constexpr int kMinWidth = 360;
constexpr double kMegabyte = 1024.0 * 1024.0;

// A size in megabytes with one decimal.
QString megabytes(long long bytes)
{
    return QString::number(static_cast<double>(bytes) / kMegabyte, 'f', 1);
}

// Makes a wrapped plain text label with a font and a color.
QLabel* textLabel(const QFont& font, const QColor& color, QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setFont(font);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
    return label;
}

}

// Builds the labels, the progress bar and the buttons, then shows the controller's state.
UpdateDialog::UpdateDialog(UpdateController& updates, QWidget* parent) : QDialog(parent), updates_(updates)
{
    const Theme& t = Theme::instance();
    setWindowTitle(tr("Update Plynth"));
    setModal(false);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(t.dialogMargin(), t.dialogMargin(), t.dialogMargin(), t.dialogMargin());
    outer->setSpacing(t.unit() * 3);

    title_ = textLabel(t.uiFont(t.sizeLarge(), QFont::DemiBold), t.text(), this);
    outer->addWidget(title_);
    versions_ = textLabel(t.monoFont(t.sizeNormal()), t.text(), this);
    outer->addWidget(versions_);
    body_ = textLabel(t.uiFont(t.sizeNormal()), t.text(), this);
    outer->addWidget(body_);
    warnings_ = textLabel(t.uiFont(t.sizeSmall()), t.muted(), this);
    outer->addWidget(warnings_);

    bar_ = new QProgressBar(this);
    bar_->setTextVisible(false);
    bar_->setFixedHeight(t.unit() * 2);
    bar_->setStyleSheet(QStringLiteral("QProgressBar { background: %1; border: none; border-radius: %3px; }"
                                       "QProgressBar::chunk { background: %2; border-radius: %3px; }")
                            .arg(t.ground().name(), t.signalBlue().name())
                            .arg(t.radius() / 2));
    outer->addWidget(bar_);
    progress_ = textLabel(t.monoFont(t.sizeSmall()), t.muted(), this);
    outer->addWidget(progress_);

    note_ = textLabel(t.uiFont(t.sizeSmall()), t.muted(), this);
    note_->setText(tr("Plynth closes while the installer runs and opens again at Home. Settings, libraries and "
                      "projects are kept."));
    outer->addWidget(note_);
    problem_ = textLabel(t.uiFont(t.sizeNormal()), t.signalAmber(), this);
    outer->addWidget(problem_);
    outer->addStretch(1);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(t.unit() * 3);
    releases_ = new QPushButton(tr("Open Releases page"), this);
    releases_->setToolTip(tr("Open the Plynth Releases page on GitHub in your browser"));
    releases_->setAutoDefault(false);
    connect(releases_, &QPushButton::clicked, this, &UpdateDialog::openReleases);
    buttons->addWidget(releases_);
    buttons->addStretch(1);
    secondary_ = new QPushButton(this);
    secondary_->setAutoDefault(false);
    connect(secondary_, &QPushButton::clicked, this, &UpdateDialog::secondaryPressed);
    buttons->addWidget(secondary_);
    primary_ = new QPushButton(this);
    primary_->setDefault(true);
    connect(primary_, &QPushButton::clicked, this, &UpdateDialog::primaryPressed);
    buttons->addWidget(primary_);
    outer->addLayout(buttons);

    const int room = parent != nullptr ? parent->width() - t.dialogMargin() * 2 : kDialogWidth;
    setFixedWidth(std::max(kMinWidth, std::min(kDialogWidth, room)));

    connect(&updates_, &UpdateController::changed, this, &UpdateDialog::refresh);
    refresh();
}

// Fills the labels and the buttons for the controller's state.
void UpdateDialog::refresh()
{
    using State = UpdateController::State;
    const State state = updates_.state();
    const QString current = updates_.currentVersion();
    const QString next = updates_.offerVersion();
    const long long total = updates_.downloadSize();

    QString title;
    QString body;
    QString problem;
    QString primary;
    QString primaryTip;
    QString secondary = tr("Close");
    QString secondaryTip = tr("Close this window. A download goes on, and Help, Update shows it again");
    bool showNote = false;
    bool showBar = false;
    bool showReleases = false;

    switch (state) {
    case State::Idle:
        title = tr("No update is known");
        body = tr("Check for updates asks GitHub for the newest signed release.");
        primary = tr("Check for updates");
        primaryTip = tr("Ask GitHub for the newest signed release now");
        break;
    case State::Checking:
        title = tr("Checking for updates");
        body = tr("Asking GitHub for the newest signed release.");
        secondary = tr("Cancel");
        secondaryTip = tr("Stop the check");
        break;
    case State::UpToDate:
        title = tr("Plynth is up to date");
        body = tr("%1 is the newest signed release.").arg(current);
        break;
    case State::CheckFailed:
        title = tr("The check failed");
        problem = updates_.message();
        primary = tr("Check again");
        primaryTip = tr("Ask GitHub for the newest signed release again");
        break;
    case State::Offer:
        title = tr("Plynth %1 is available").arg(next);
        body = (updates_.offer() && updates_.offer()->earlyPreview ? tr("This is an early preview. ") : QString()) +
               tr("Update downloads the installer and checks its signature. You can keep working while it downloads.");
        primary = tr("Update");
        primaryTip = tr("Download and verify the installer of Plynth %1").arg(next);
        showNote = true;
        showReleases = true;
        break;
    case State::Downloading:
        title = tr("Downloading Plynth %1").arg(next);
        body = tr("You can keep working. The installer runs only when its signature is verified.");
        secondary = tr("Cancel");
        secondaryTip = tr("Stop the download. Nothing is installed");
        showNote = true;
        showBar = true;
        break;
    case State::Verified:
        title = tr("Ready to install Plynth %1").arg(next);
        body = tr("The installer is downloaded and its signature is verified.");
        problem = updates_.message();
        primary = tr("Install now");
        primaryTip = tr("Ask about unsaved changes, start the installer and close Plynth");
        secondary = tr("Cancel");
        secondaryTip = tr("Let the installer go. Nothing is installed");
        showNote = true;
        break;
    case State::Refused:
        title = tr("Finish this first");
        problem = updates_.message();
        if (updates_.hasInstaller()) {
            primary = tr("Install now");
            primaryTip = tr("Check again and start the installer when nothing is busy");
            secondary = tr("Cancel");
            secondaryTip = tr("Let the installer go. Nothing is installed");
        } else {
            primary = tr("Try again");
            primaryTip = tr("Check again and start the download when nothing is busy");
        }
        showNote = true;
        break;
    case State::Failed:
        title = tr("The update failed");
        problem = updates_.message();
        body = tr("Nothing was installed.");
        primary = tr("Try again");
        primaryTip = tr("Check and download again");
        break;
    case State::Starting:
        title = tr("Starting the installer");
        body = tr("Plynth closes now and opens again at Home when the installer is done.");
        secondary.clear();
        break;
    case State::NotInstalled:
        title = tr("This copy cannot update itself");
        body = QString::fromUtf8(plynth::kNotInstalledMessage.data(),
                                 static_cast<qsizetype>(plynth::kNotInstalledMessage.size()));
        showReleases = true;
        break;
    }

    title_->setText(title);
    QString versions;
    if (!next.isEmpty() && state != State::NotInstalled)
        versions = tr("This version %1    New version %2").arg(current, next);
    if (total > 0 && !versions.isEmpty())
        versions += tr("    Size %1 MB").arg(megabytes(total));
    versions_->setText(versions);
    versions_->setVisible(!versions.isEmpty());
    body_->setText(body);
    body_->setVisible(!body.isEmpty());
    const QStringList warnings = updates_.warnings();
    warnings_->setText(warnings.join(QLatin1Char('\n')));
    warnings_->setVisible(!warnings.isEmpty() && state != State::NotInstalled);

    bar_->setVisible(showBar);
    progress_->setVisible(showBar);
    if (showBar) {
        if (total > 0) {
            bar_->setRange(0, 1000);
            bar_->setValue(static_cast<int>(std::min<long long>(1000, updates_.downloaded() * 1000 / total)));
            progress_->setText(tr("%1 of %2 MB").arg(megabytes(updates_.downloaded()), megabytes(total)));
        } else {
            bar_->setRange(0, 0);
            progress_->setText(tr("%1 MB").arg(megabytes(updates_.downloaded())));
        }
    }
    note_->setVisible(showNote);
    problem_->setText(problem);
    problem_->setVisible(!problem.isEmpty());

    primary_->setText(primary);
    primary_->setToolTip(primaryTip);
    primary_->setVisible(!primary.isEmpty());
    secondary_->setText(secondary);
    secondary_->setToolTip(secondaryTip);
    secondary_->setVisible(!secondary.isEmpty());
    releases_->setVisible(showReleases);
    adjustSize();
}

// Sends the main button to the controller.
void UpdateDialog::primaryPressed()
{
    using State = UpdateController::State;
    switch (updates_.state()) {
    case State::Idle:
    case State::CheckFailed:
        updates_.checkByHand();
        break;
    case State::Offer:
    case State::Failed:
        updates_.update();
        break;
    case State::Verified:
        updates_.install();
        break;
    case State::Refused:
        if (updates_.hasInstaller())
            updates_.install();
        else
            updates_.update();
        break;
    default:
        break;
    }
}

// Cancel stops the work. Close only hides the window, so a download goes on.
void UpdateDialog::secondaryPressed()
{
    using State = UpdateController::State;
    const State state = updates_.state();
    if (state == State::Checking || state == State::Downloading || state == State::Verified ||
        (state == State::Refused && updates_.hasInstaller()))
        updates_.cancel();
    hide();
}

// Opens the Releases page, or the page of the offered release, only when the button is pressed.
void UpdateDialog::openReleases()
{
    if (updates_.canned())
        return;
    const bool release = updates_.state() != UpdateController::State::NotInstalled && updates_.offer() &&
                         !updates_.offer()->releaseUrl.empty();
    const std::string_view link = release ? std::string_view(updates_.offer()->releaseUrl) : plynth::kReleasesPage;
    QDesktopServices::openUrl(QUrl(QString::fromUtf8(link.data(), static_cast<qsizetype>(link.size()))));
}

}
