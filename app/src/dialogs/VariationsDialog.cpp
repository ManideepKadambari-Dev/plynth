#include "VariationsDialog.h"

#include "theme/Theme.h"

#include <plynth/core/VariationCopies.h>

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QUrl>
#include <QVBoxLayout>

#include <limits>

namespace plynth::app {

namespace {

// A path in the form the owner sees in Explorer.
QString nativePath(const std::filesystem::path& path)
{
    return QDir::toNativeSeparators(QString::fromStdWString(path.wstring()));
}

// A muted label for the left column.
QLabel* makeName(const QString& text, QWidget* parent)
{
    const Theme& t = Theme::instance();
    auto* label = new QLabel(text, parent);
    label->setFont(t.uiFont(t.sizeSmall()));
    label->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    return label;
}

}

// Builds the seeds and folder boxes, the Make button and the list of folders made.
VariationsDialog::VariationsDialog(const plynth::Project& project, const std::filesystem::path& projectFolder,
                                   QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint), project_(project), projectFolder_(projectFolder)
{
    const Theme& t = Theme::instance();
    setWindowTitle(tr("Make variations"));
    setModal(true);
    setProperty("tourId", QStringLiteral("dialog-variations"));
    setMinimumWidth(t.unit() * 130);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(t.unit() * 5, t.unit() * 5, t.unit() * 5, t.unit() * 4);
    layout->setSpacing(t.unit() * 3);

    auto* intro = new QLabel(tr("Saves a copy of this project for each seed. Each copy builds its own variation of "
                                "furniture, materials and props, and its files never clash with the others."),
                             this);
    intro->setWordWrap(true);
    intro->setFont(t.uiFont(t.sizeNormal()));
    layout->addWidget(intro);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(t.unit() * 3);
    grid->setVerticalSpacing(t.unit() * 2);
    grid->setColumnStretch(1, 1);

    const QString seedsTip = tr("Seeds separated by commas, with ranges such as 5-8. At most %1 at once.")
                                 .arg(plynth::kMaxVariationCopies);
    seeds_ = new QLineEdit(QStringLiteral("1-4"), this);
    seeds_->setFont(t.monoFont(t.sizeNormal()));
    seeds_->setToolTip(seedsTip);
    seeds_->setProperty("tourId", QStringLiteral("variations-seeds"));
    QLabel* seedsName = makeName(tr("Seeds"), this);
    seedsName->setToolTip(seedsTip);
    grid->addWidget(seedsName, 0, 0);
    grid->addWidget(seeds_, 0, 1, 1, 2);

    const QString folderTip = tr("The folder the copies go in. Each copy is a folder named after the project and its seed.");
    folder_ = new QLineEdit(nativePath(projectFolder.parent_path()), this);
    folder_->setFont(t.monoFont(t.sizeNormal()));
    folder_->setToolTip(folderTip);
    folder_->setProperty("tourId", QStringLiteral("variations-folder"));
    auto* browse = new QPushButton(tr("Browse..."), this);
    browse->setToolTip(tr("Choose the folder for the copies"));
    browse->setProperty("tourId", QStringLiteral("variations-browse"));
    QLabel* folderName = makeName(tr("Folder"), this);
    folderName->setToolTip(folderTip);
    grid->addWidget(folderName, 1, 0);
    grid->addWidget(folder_, 1, 1);
    grid->addWidget(browse, 1, 2);
    layout->addLayout(grid);

    result_ = new QLabel(this);
    result_->setWordWrap(true);
    result_->setFont(t.uiFont(t.sizeNormal()));
    result_->setToolTip(tr("What the last Make did"));
    layout->addWidget(result_);

    made_ = new QListWidget(this);
    made_->setFont(t.monoFont(t.sizeSmall()));
    made_->setToolTip(tr("The project folders made. Open one from the home screen to build it."));
    made_->setProperty("tourId", QStringLiteral("variations-results"));
    made_->setMinimumHeight(t.unit() * 30);
    layout->addWidget(made_, 1);

    auto* buttons = new QDialogButtonBox(this);
    make_ = buttons->addButton(tr("Make copies"), QDialogButtonBox::ActionRole);
    make_->setDefault(true);
    make_->setToolTip(tr("Save one project folder for each seed. Nothing is overwritten."));
    make_->setProperty("tourId", QStringLiteral("variations-make"));
    open_ = buttons->addButton(tr("Open folder"), QDialogButtonBox::ActionRole);
    open_->setToolTip(tr("Open the folder of the copies in Explorer"));
    open_->setProperty("tourId", QStringLiteral("variations-open-folder"));
    QPushButton* close = buttons->addButton(tr("Close"), QDialogButtonBox::RejectRole);
    close->setToolTip(tr("Close this window"));
    layout->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, &VariationsDialog::chooseFolder);
    connect(make_, &QPushButton::clicked, this, &VariationsDialog::make);
    connect(open_, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder_->text()));
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(seeds_, &QLineEdit::textChanged, this, &VariationsDialog::updateButtons);
    connect(folder_, &QLineEdit::textChanged, this, &VariationsDialog::updateButtons);
    updateButtons();
}

// Reads a list of seeds and ranges. Every seed is a whole number from 0 to the largest 32 bit number.
std::optional<std::vector<std::uint32_t>> VariationsDialog::parseSeeds(const QString& text)
{
    static const QRegularExpression range(QStringLiteral("^\\s*(\\d+)\\s*(?:-\\s*(\\d+)\\s*)?$"));
    std::vector<std::uint32_t> seeds;
    for (const QString& part : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch m = range.match(part);
        if (!m.hasMatch())
            return std::nullopt;
        bool okFrom = false;
        bool okTo = true;
        const qulonglong from = m.captured(1).toULongLong(&okFrom);
        const qulonglong to = m.captured(2).isEmpty() ? from : m.captured(2).toULongLong(&okTo);
        if (!okFrom || !okTo || to < from || to > std::numeric_limits<std::uint32_t>::max())
            return std::nullopt;
        // A long range stops early; core refuses more than its limit anyway.
        for (qulonglong seed = from; seed <= to && seeds.size() <= plynth::kMaxVariationCopies; ++seed)
            seeds.push_back(static_cast<std::uint32_t>(seed));
    }
    if (seeds.empty())
        return std::nullopt;
    return seeds;
}

// Fills the seeds box.
void VariationsDialog::setSeedsText(const QString& text)
{
    seeds_->setText(text);
}

// Asks for the output folder.
void VariationsDialog::chooseFolder()
{
    const QString chosen = QFileDialog::getExistingDirectory(this, tr("Folder for the copies"), folder_->text());
    if (!chosen.isEmpty())
        folder_->setText(QDir::toNativeSeparators(chosen));
}

// Runs makeVariationCopies and shows the folders made, or why none were.
void VariationsDialog::make()
{
    const Theme& t = Theme::instance();
    const std::optional<std::vector<std::uint32_t>> seeds = parseSeeds(seeds_->text());
    if (!seeds)
        return;
    const std::filesystem::path out(folder_->text().toStdWString());
    const plynth::VariationCopies copies = plynth::makeVariationCopies(project_, projectFolder_, *seeds, out);
    made_->clear();
    if (!copies.status.ok()) {
        result_->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
        result_->setText(QString::fromStdString(copies.status.message));
        return;
    }
    result_->setStyleSheet(QStringLiteral("color: %1;").arg(t.text().name()));
    result_->setText(tr("Made %n copies.", nullptr, static_cast<int>(copies.folders.size())));
    for (const std::filesystem::path& folder : copies.folders)
        made_->addItem(nativePath(folder));
    updateButtons();
}

// Make needs readable seeds and a folder. Open folder needs the folder to be there.
void VariationsDialog::updateButtons()
{
    const bool seedsOk = parseSeeds(seeds_->text()).has_value();
    seeds_->setStyleSheet(seedsOk ? QString()
                                  : QStringLiteral("color: %1;").arg(Theme::instance().signalAmber().name()));
    make_->setEnabled(seedsOk && !folder_->text().trimmed().isEmpty() && !projectFolder_.empty());
    open_->setEnabled(QDir(folder_->text()).exists());
}

}
