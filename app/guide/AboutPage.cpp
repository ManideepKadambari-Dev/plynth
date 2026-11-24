#include "AboutPage.h"

#include <plynth/core/About.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

namespace plynth::guide {

namespace {

// Text from core as a Qt string.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// A label that wraps its text.
QLabel* sentence(const QString& text, const QFont& font, bool muted, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setFont(font);
    label->setWordWrap(true);
    label->setProperty("muted", muted);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

}

// Reads the About facts from core and lays them out top to bottom.
AboutPage::AboutPage(const GuideStyle& style, const QPixmap& logo, const std::filesystem::path& licensesFolder,
                     QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("guideRoot"));
    setProperty("tourId", QStringLiteral("about-page"));
    setStyleSheet(guideStyleSheet(style));
    const plynth::AboutInfo info = plynth::aboutInfo(licensesFolder);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(style.unit * 6, style.unit * 6, style.unit * 6, style.unit * 6);
    column->setSpacing(style.unit * 3);

    auto* head = new QHBoxLayout;
    head->setSpacing(style.unit * 4);
    auto* picture = new QLabel(this);
    picture->setToolTip(tr("The Plynth logo. It is not part of the CC0 samples"));
    if (!logo.isNull()) picture->setPixmap(logo.scaled(style.unit * 16, style.unit * 16, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    head->addWidget(picture, 0, Qt::AlignTop);
    auto* titles = new QVBoxLayout;
    auto* name = new QLabel(QStringLiteral("Plynth"), this);
    name->setFont(style.titleFont);
    titles->addWidget(name);
    auto* version = new QLabel(tr("Version %1").arg(fromCore(info.version)), this);
    version->setFont(style.monoFont);
    version->setProperty("tourId", QStringLiteral("about-version"));
    version->setToolTip(tr("The version of this copy of Plynth"));
    version->setTextInteractionFlags(Qt::TextSelectableByMouse);
    titles->addWidget(version);
    titles->addWidget(sentence(fromCore(info.copyright), style.uiFont, true, this));
    titles->addStretch(1);
    head->addLayout(titles, 1);
    column->addLayout(head);

    column->addWidget(sentence(tr("Plynth's source is %1. This program includes Qt Quick 3D and Qt Quick Timeline, "
                                  "so it is distributed under GPL version 3.")
                                   .arg(fromCore(info.sourceLicense)),
                               style.uiFont, false, this));
    column->addWidget(sentence(tr("The sample plans, models, materials, shapes and projects are dedicated to the "
                                  "public domain under %1.")
                                   .arg(fromCore(info.sampleLicense)),
                               style.uiFont, false, this));
    column->addWidget(sentence(tr("The logo is not part of the CC0 samples."), style.uiFont, true, this));
    if (!info.status.ok()) {
        column->addWidget(sentence(tr("The license notices are missing from this copy of Plynth."), style.uiFont, true, this));
    }

    auto* lists = new QHBoxLayout;
    lists->setSpacing(style.unit * 3);
    notices_ = new QTextBrowser(this);
    notices_->setProperty("tourId", QStringLiteral("about-notices"));
    notices_->setToolTip(tr("Every library, font and data file Plynth ships, with its license"));
    notices_->setOpenLinks(false);
    notices_->setFont(style.uiFont);
    notices_->document()->setDefaultFont(style.uiFont);
    notices_->document()->setMarkdown(fromCore(info.notices),
                          QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub | QTextDocument::MarkdownNoHTML));
    connect(notices_, &QTextBrowser::anchorClicked, this, &AboutPage::linkClicked);
    lists->addWidget(notices_, 3);

    texts_ = new QListWidget(this);
    texts_->setProperty("tourId", QStringLiteral("about-licenses"));
    texts_->setToolTip(tr("The full license texts. Click one to read it"));
    texts_->setFont(style.monoFont);
    const std::filesystem::path textsFolder = licensesFolder / std::filesystem::path(u8"texts");
    for (const std::filesystem::path& file : info.licenseFiles) {
        const std::filesystem::path relative = file.lexically_relative(textsFolder);
        auto* item = new QListWidgetItem(QString::fromStdWString(relative.generic_wstring()), texts_);
        item->setData(Qt::UserRole, QString::fromStdWString(file.wstring()));
    }
    connect(texts_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        emit licenseFileRequested(std::filesystem::path(item->data(Qt::UserRole).toString().toStdWString()));
    });
    connect(texts_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        emit licenseFileRequested(std::filesystem::path(item->data(Qt::UserRole).toString().toStdWString()));
    });
    if (style.listWidth > 0) texts_->setMaximumWidth(style.listWidth);
    lists->addWidget(texts_, 1);
    column->addLayout(lists, 1);
}

}
