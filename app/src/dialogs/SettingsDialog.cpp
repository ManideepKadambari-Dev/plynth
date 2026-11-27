#include "SettingsDialog.h"

#include "theme/Theme.h"

#include <plynth/core/AgentFiles.h>

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <filesystem>
#include <string>

namespace plynth::app {

namespace {

// Turns a Qt path into a standard path without losing non English letters.
std::filesystem::path toPath(const QString& text)
{
    return std::filesystem::path(text.toStdWString());
}

// Turns a standard path into Qt text with the separators Windows shows.
QString fromPath(const std::filesystem::path& path)
{
    return QDir::toNativeSeparators(QString::fromStdWString(path.wstring()));
}

// Turns a core sentence into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}

// Builds the window: the coding assistant section with its text, its button and the line for the result.
SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint)
{
    const Theme& t = Theme::instance();
    setWindowTitle(tr("Settings"));
    setModal(true);
    setMinimumWidth(t.unit() * 140);
    lastFolder_ = QDir::homePath();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(t.dialogMargin(), t.dialogMargin(), t.dialogMargin(), t.dialogMargin());
    layout->setSpacing(t.unit() * 2);

    auto* title = new QLabel(tr("Settings"), this);
    title->setFont(t.uiFont(t.sizeLarge(), QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color: %1;").arg(t.text().name()));
    layout->addWidget(title);
    layout->addSpacing(t.unit() * 2);

    auto* heading = new QLabel(tr("Coding assistant"), this);
    heading->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    heading->setStyleSheet(QStringLiteral("color: %1;").arg(t.text().name()));
    layout->addWidget(heading);

    auto* intro = new QLabel(tr("Give a coding assistant you run yourself the steps to make a Plynth project from a "
                                "plan image. Choose the skills folder that your assistant's documentation names."),
                             this);
    intro->setTextFormat(Qt::PlainText);
    intro->setWordWrap(true);
    intro->setFont(t.uiFont(t.sizeNormal()));
    intro->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    intro->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(intro);

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    setUp_ = new QPushButton(tr("Set up my coding assistant..."), this);
    setUp_->setToolTip(tr("Choose the skills folder of your coding assistant. Plynth writes plynth\\SKILL.md in it"));
    connect(setUp_, &QPushButton::clicked, this, &SettingsDialog::chooseSkillsFolder);
    row->addWidget(setUp_);
    row->addStretch(1);
    layout->addLayout(row);

    result_ = new QLabel(this);
    result_->setTextFormat(Qt::RichText);
    result_->setWordWrap(true);
    result_->setFont(t.uiFont(t.sizeSmall()));
    result_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    result_->setToolTip(tr("What the last set up did, or why it could not"));
    result_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    result_->hide();
    layout->addWidget(result_);

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* close = buttons->addButton(tr("Close"), QDialogButtonBox::RejectRole);
    close->setToolTip(tr("Close the settings"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addSpacing(t.unit() * 2);
    layout->addWidget(buttons);
    adjustSize();
}

// Paints the Raised panel with the theme's border and corner radius.
void SettingsDialog::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), t.ground());
    p.setPen(t.line());
    p.setBrush(t.raised());
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radius(), t.radius());
}

// Asks for the skills folder, then writes the skill file there.
void SettingsDialog::chooseSkillsFolder()
{
    const QString folder =
        QFileDialog::getExistingDirectory(this, tr("Choose the skills folder of your coding assistant"), lastFolder_);
    if (folder.isEmpty())
        return;
    lastFolder_ = folder;
    installInto(folder, true);
}

// Writes the skill file through core. A file that is there is kept unless the user agrees to replace it.
bool SettingsDialog::installInto(const QString& skillsFolder, bool askBeforeReplace)
{
    const std::filesystem::path folder = toPath(skillsFolder);
    plynth::AgentFileWrite write = plynth::installSkill(folder, false);
    if (write.status.ok() && write.result == plynth::AgentFileResult::Kept) {
        const QString file = fromPath(write.file);
        if (!askBeforeReplace || !askReplace(file)) {
            showResult(tr("The skill file %1 is there already and was kept.").arg(file), false);
            return false;
        }
        write = plynth::installSkill(folder, true);
    }
    if (!write.status.ok()) {
        showResult(tr("Not set up. %1").arg(fromCore(write.status.message)), true);
        return false;
    }
    showResult(tr("Wrote %1. Start your coding assistant again so it reads the skill.").arg(fromPath(write.file)), false);
    return true;
}

// Asks whether to replace the skill file that is there. True when the user chose Replace.
bool SettingsDialog::askReplace(const QString& file)
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::NoIcon);
    box.setTextFormat(Qt::PlainText);
    box.setWindowTitle(tr("Replace the skill file"));
    box.setText(tr("%1 is there already. Replace it with the one from this version of Plynth? Changes made to it "
                   "are lost.")
                    .arg(file));
    QPushButton* replace = box.addButton(tr("Replace"), QMessageBox::DestructiveRole);
    QPushButton* keep = box.addButton(tr("Keep"), QMessageBox::RejectRole);
    replace->setToolTip(tr("Write the skill file of this version over the one that is there"));
    keep->setToolTip(tr("Leave the skill file as it is"));
    box.setDefaultButton(keep);
    box.setEscapeButton(keep);
    box.exec();
    return box.clickedButton() == replace;
}

// Shows the result under the button: in the text color when it worked or kept the file, in amber when refused.
void SettingsDialog::showResult(const QString& text, bool refused)
{
    const Theme& t = Theme::instance();
    result_->setStyleSheet(QStringLiteral("color: %1;").arg((refused ? t.signalAmber() : t.text()).name()));
    result_->setText(t.withMonoPaths(text));
    result_->show();
    adjustSize();
}

}
