#include "NewProjectDialog.h"

#include "theme/ElideLabel.h"
#include "theme/Theme.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace plynth::app {

namespace {

// True for a name Windows keeps for a device, such as CON or COM1, with or without an ending.
bool isDeviceName(const QString& name)
{
    static const QRegularExpression device(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?$"),
                                           QRegularExpression::CaseInsensitiveOption);
    return device.match(name).hasMatch();
}

}

// Builds the dialog with the name, the location with Browse, the folder it makes and the buttons.
NewProjectDialog::NewProjectDialog(const QString& location, const QString& suggestedName, QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint)
{
    const Theme& t = Theme::instance();
    setWindowTitle(tr("New project"));
    setModal(true);
    setMinimumWidth(t.unit() * 140);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(t.dialogMargin(), t.dialogMargin(), t.dialogMargin(), t.dialogMargin());
    layout->setSpacing(t.unit() * 2);

    auto* title = new QLabel(tr("New project"), this);
    title->setFont(t.uiFont(t.sizeLarge(), QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color: %1;").arg(t.text().name()));
    layout->addWidget(title);
    layout->addSpacing(t.unit() * 2);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(t.unit() * 3);
    grid->setVerticalSpacing(t.unit() * 2);
    auto* nameLabel = new QLabel(tr("Name"), this);
    nameLabel->setFont(t.uiFont(t.sizeNormal()));
    grid->addWidget(nameLabel, 0, 0);
    name_ = new QLineEdit(suggestedName, this);
    name_->setFont(t.uiFont(t.sizeNormal()));
    name_->setMinimumWidth(t.unit() * 90);
    name_->setToolTip(tr("The name of the project. It is also the name of its folder"));
    nameLabel->setBuddy(name_);
    grid->addWidget(name_, 0, 1, 1, 2);

    auto* locationLabel = new QLabel(tr("Location"), this);
    locationLabel->setFont(t.uiFont(t.sizeNormal()));
    grid->addWidget(locationLabel, 1, 0);
    location_ = new QLineEdit(QDir::toNativeSeparators(location), this);
    location_->setFont(t.monoFont(t.sizeNormal()));
    location_->setToolTip(tr("The folder the project folder is made in"));
    locationLabel->setBuddy(location_);
    grid->addWidget(location_, 1, 1);
    auto* browse = new QPushButton(tr("Browse..."), this);
    browse->setToolTip(tr("Choose the folder the project folder is made in"));
    connect(browse, &QPushButton::clicked, this, &NewProjectDialog::browse);
    grid->addWidget(browse, 1, 2);
    targetCaption_ = new QLabel(tr("Folder"), this);
    targetCaption_->setFont(t.uiFont(t.sizeSmall()));
    targetCaption_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    grid->addWidget(targetCaption_, 2, 0);
    target_ = new ElideLabel(this);
    target_->setFont(t.monoFont(t.sizeSmall()));
    target_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    target_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    grid->addWidget(target_, 2, 1, 1, 2);

    problem_ = new QLabel(this);
    problem_->setTextFormat(Qt::PlainText);
    problem_->setFont(t.uiFont(t.sizeSmall()));
    problem_->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
    problem_->setWordWrap(true);
    problem_->setToolTip(tr("Why the project cannot be made yet"));
    problem_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    // The folder and the reason share one row: only one of them shows at a time.
    grid->addWidget(problem_, 2, 0, 1, 3);
    grid->setColumnStretch(1, 1);
    layout->addLayout(grid);

    auto* buttons = new QDialogButtonBox(this);
    create_ = buttons->addButton(tr("Create"), QDialogButtonBox::AcceptRole);
    QPushButton* cancel = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    create_->setDefault(true);
    create_->setToolTip(tr("Make the project folder, save the new project in it and open it"));
    cancel->setToolTip(tr("Close without making a project"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        validate();
        if (create_->isEnabled())
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addSpacing(t.unit() * 2);
    layout->addWidget(buttons);

    connect(name_, &QLineEdit::textChanged, this, &NewProjectDialog::validate);
    connect(location_, &QLineEdit::textChanged, this, &NewProjectDialog::validate);
    validate();
    name_->selectAll();
    name_->setFocus();
}

// Paints the Raised panel with the theme's border and corner radius.
void NewProjectDialog::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), t.ground());
    p.setPen(t.line());
    p.setBrush(t.raised());
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radius(), t.radius());
}

// The name as typed, without spaces at either end.
QString NewProjectDialog::name() const
{
    return name_->text().trimmed();
}

// The location with the name added.
QString NewProjectDialog::folder() const
{
    return QDir::toNativeSeparators(QDir::cleanPath(QDir(location_->text().trimmed()).filePath(name())));
}

// Fills both fields, for checks of the dialog without a person.
void NewProjectDialog::setFields(const QString& name, const QString& location)
{
    name_->setText(name);
    if (!location.isEmpty())
        location_->setText(QDir::toNativeSeparators(location));
}

// Asks for the location folder.
void NewProjectDialog::browse()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Choose the location"), location_->text().trimmed());
    if (!folder.isEmpty())
        location_->setText(QDir::toNativeSeparators(folder));
}

// Shows the folder Create would make, or the sentence that says why it cannot, and turns Create on or off.
void NewProjectDialog::validate()
{
    const QString why = problem(location_->text().trimmed(), name());
    problem_->setText(why);
    problem_->setVisible(!why.isEmpty());
    const bool ready = why.isEmpty();
    target_->setText(ready ? folder() : QString());
    target_->setToolTip(ready ? tr("Create makes this folder and saves the project in it: %1").arg(folder()) : QString());
    target_->setVisible(ready);
    targetCaption_->setVisible(ready);
    create_->setEnabled(ready);
    adjustSize();
}

// Tries base, then base with 2, 3 and so on after it, until no folder of that name is in location.
QString NewProjectDialog::freeName(const QString& location, const QString& base)
{
    const QDir dir(location);
    if (location.isEmpty() || !QFileInfo::exists(dir.filePath(base)))
        return base;
    for (int number = 2; number < 1000; ++number) {
        const QString name = QStringLiteral("%1 %2").arg(base).arg(number);
        if (!QFileInfo::exists(dir.filePath(name)))
            return name;
    }
    return base;
}

// Checks the name as a Windows folder name, then the location, then the folder they make together.
QString NewProjectDialog::problem(const QString& location, const QString& name)
{
    if (name.isEmpty())
        return tr("Type a name for the project.");
    static const QRegularExpression forbidden(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1F]"));
    if (name.contains(forbidden))
        return tr("A folder name cannot contain any of these characters: \\ / : * ? \" < > |");
    if (name.endsWith(QLatin1Char('.')) || name == QStringLiteral(".") || name == QStringLiteral(".."))
        return tr("A folder name cannot end with a dot.");
    if (isDeviceName(name))
        return tr("Windows keeps the name %1 for itself. Choose another name.").arg(name);
    if (location.isEmpty())
        return tr("Choose a location for the project.");
    const QFileInfo place(location);
    if (place.exists() && !place.isDir())
        return tr("The location %1 is a file, not a folder.").arg(QDir::toNativeSeparators(location));
    if (place.isRelative())
        return tr("Choose a full location, such as one picked with Browse.");
    const QString target = QDir(location).filePath(name);
    const QFileInfo folder(target);
    if (folder.exists() && !folder.isDir())
        return tr("The location already has a file named %1. Choose another name.").arg(name);
    if (folder.isDir() && !QDir(target).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
        return tr("The location already has a folder named %1 with files in it. Choose another name or location.").arg(name);
    return QString();
}

}
