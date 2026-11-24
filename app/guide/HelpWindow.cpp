#include "HelpWindow.h"

#include "AboutPage.h"

#include <plynth/core/ProjectFiles.h>

#include <QDesktopServices>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFormat>
#include <QTextFragment>
#include <QTimer>
#include <QVBoxLayout>

namespace plynth::guide {

namespace {

constexpr int kPageIndex = 0;
constexpr int kResultsIndex = 1;
constexpr int kAboutIndex = 2;

// Text from core as a Qt string.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// A Qt string as UTF-8 for core.
std::string toCore(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

// Sets every code span and code block of a document in the mono font.
void monoForCode(QTextDocument* document, const QFont& mono)
{
    QTextCursor cursor(document);
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().fontFixedPitch()) continue;
            cursor.setPosition(fragment.position());
            cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            QTextCharFormat format;
            format.setFontFamilies({mono.family()});
            cursor.mergeCharFormat(format);
        }
    }
}

// A button with its tooltip and tour id.
QPushButton* navButton(const QString& text, const QString& tip, const QString& tourId, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setToolTip(tip);
    button->setProperty("tourId", tourId);
    return button;
}

}

// Lays out the page list and search on the left and the page on the right.
HelpWindow::HelpWindow(const GuideStyle& style, const std::filesystem::path& docs,
                       const std::filesystem::path& licenses, const QPixmap& logo, QWidget* parent)
    : QWidget(parent, Qt::Window), style_(style), docs_(docs), licenses_(licenses)
{
    setObjectName(QStringLiteral("guideRoot"));
    setProperty("tourId", QStringLiteral("help-window"));
    setWindowTitle(tr("Plynth help"));
    setStyleSheet(guideStyleSheet(style));
    setFont(style.uiFont);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(style.unit * 3, style.unit * 3, style.unit * 3, style.unit * 3);
    row->setSpacing(style.unit * 3);

    auto* left = new QVBoxLayout;
    left->setSpacing(style.unit * 2);
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search help"));
    search_->setToolTip(tr("Type words to find them in every help page"));
    search_->setProperty("tourId", QStringLiteral("help-search"));
    search_->setClearButtonEnabled(true);
    left->addWidget(search_);
    pages_ = new QListWidget(this);
    pages_->setToolTip(tr("The help pages. Click one to read it"));
    pages_->setProperty("tourId", QStringLiteral("help-pages"));
    for (const plynth::HelpPageEntry& entry : plynth::helpPages()) {
        if (!entry.listed) continue;
        if (entry.kind == plynth::HelpPageKind::File) {
            std::error_code ec;
            if (docs_.empty() || !std::filesystem::is_regular_file(docs_ / plynth::pathFromUtf8(entry.file), ec)) continue;
        }
        auto* item = new QListWidgetItem(fromCore(entry.title), pages_);
        item->setData(Qt::UserRole, fromCore(entry.id));
    }
    left->addWidget(pages_, 1);
    auto* leftBox = new QWidget(this);
    leftBox->setLayout(left);
    left->setContentsMargins(0, 0, 0, 0);
    if (style.listWidth > 0) leftBox->setFixedWidth(style.listWidth);
    row->addWidget(leftBox);

    auto* right = new QVBoxLayout;
    right->setSpacing(style.unit * 2);
    auto* bar = new QHBoxLayout;
    back_ = navButton(tr("Back"), tr("Go back to the page you saw before (Alt+Left)"), QStringLiteral("help-back"), this);
    forward_ = navButton(tr("Forward"), tr("Go forward again (Alt+Right)"), QStringLiteral("help-forward"), this);
    back_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
    forward_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Right));
    title_ = new QLabel(this);
    title_->setFont(style.titleFont);
    bar->addWidget(back_);
    bar->addWidget(forward_);
    bar->addSpacing(style.unit * 2);
    bar->addWidget(title_, 1);
    right->addLayout(bar);

    stack_ = new QStackedWidget(this);
    page_ = new QTextBrowser(stack_);
    page_->setProperty("tourId", QStringLiteral("help-page"));
    page_->setToolTip(tr("The help page. Links to other pages open here, web links in your browser"));
    page_->setOpenLinks(false);
    page_->setOpenExternalLinks(false);
    page_->document()->setDefaultFont(style.uiFont);
    page_->document()->setDocumentMargin(style.unit * 4);
    results_ = new QListWidget(stack_);
    results_->setProperty("tourId", QStringLiteral("help-results"));
    results_->setToolTip(tr("Every place the words appear, headings first. Click one to open the page there"));
    results_->setWordWrap(true);
    about_ = new AboutPage(style, logo, licenses_, stack_);
    stack_->insertWidget(kPageIndex, page_);
    stack_->insertWidget(kResultsIndex, results_);
    stack_->insertWidget(kAboutIndex, about_);
    right->addWidget(stack_, 1);
    row->addLayout(right, 1);

    searchTimer_ = new QTimer(this);
    searchTimer_->setSingleShot(true);
    searchTimer_->setInterval(250);
    connect(searchTimer_, &QTimer::timeout, this, &HelpWindow::runSearch);
    connect(search_, &QLineEdit::textChanged, searchTimer_, qOverload<>(&QTimer::start));
    connect(search_, &QLineEdit::returnPressed, this, &HelpWindow::runSearch);
    connect(pages_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        showPage(item->data(Qt::UserRole).toString());
    });
    connect(results_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        showPage(item->data(Qt::UserRole).toString(), item->data(Qt::UserRole + 1).toString());
    });
    connect(page_, &QTextBrowser::anchorClicked, this, &HelpWindow::followLink);
    connect(about_, &AboutPage::linkClicked, this, &HelpWindow::followLink);
    connect(about_, &AboutPage::licenseFileRequested, this, [this](const std::filesystem::path& file) {
        go(Place{QString(), QString(), file}, true);
    });
    connect(back_, &QPushButton::clicked, this, [this] {
        if (historyIndex_ > 0) go(history_[static_cast<std::size_t>(--historyIndex_)], false);
    });
    connect(forward_, &QPushButton::clicked, this, [this] {
        if (historyIndex_ + 1 < static_cast<int>(history_.size())) go(history_[static_cast<std::size_t>(++historyIndex_)], false);
    });
    updateButtons();
}

// Shows a page, or asks for the setup check when that row is picked.
void HelpWindow::showPage(const QString& id, const QString& anchor)
{
    const plynth::HelpPageEntry* entry = plynth::helpPage(toCore(id));
    if (!entry) return;
    if (entry->kind == plynth::HelpPageKind::SetupCheck) {
        emit setupCheckRequested();
        return;
    }
    go(Place{id, anchor, {}}, true);
}

// Fills the search box and searches at once.
void HelpWindow::showSearch(const QString& text)
{
    search_->setText(text);
    searchTimer_->stop();
    runSearch();
}

// Shows a place and, when remember is true, adds it to the history.
void HelpWindow::go(const Place& place, bool remember)
{
    if (!place.licenseFile.empty()) {
        showLicenseFile(place.licenseFile);
    } else {
        const plynth::HelpPageEntry* entry = plynth::helpPage(toCore(place.page));
        if (!entry) return;
        title_->setText(fromCore(entry->title));
        for (int i = 0; i < pages_->count(); ++i) {
            if (pages_->item(i)->data(Qt::UserRole).toString() == place.page) pages_->setCurrentRow(i);
        }
        if (entry->kind == plynth::HelpPageKind::About) {
            stack_->setCurrentIndex(kAboutIndex);
            headings_.clear();
        } else {
            if (place.page != current_.page || !current_.licenseFile.empty() || stack_->currentIndex() != kPageIndex) {
                const plynth::HelpPageRead read = plynth::readHelpPage(docs_, toCore(place.page));
                headings_ = read.headings;
                page_->setFont(style_.uiFont);
                page_->document()->setDefaultFont(style_.uiFont);
                if (read.status.ok()) {
                    page_->setMarkdown(fromCore(read.markdown),
                                       QTextDocument::MarkdownDialectGitHub | QTextDocument::MarkdownNoHTML);
                    monoForCode(page_->document(), style_.monoFont);
                } else {
                    page_->setPlainText(fromCore(read.status.message));
                }
            }
            stack_->setCurrentIndex(kPageIndex);
            if (place.anchor.isEmpty()) page_->verticalScrollBar()->setValue(0);
            else scrollToAnchor(place.anchor);
        }
    }
    current_ = place;
    if (remember && (historyIndex_ < 0 || history_[static_cast<std::size_t>(historyIndex_)] != place)) {
        history_.resize(static_cast<std::size_t>(historyIndex_ + 1));
        history_.push_back(place);
        historyIndex_ = static_cast<int>(history_.size()) - 1;
    }
    updateButtons();
}

// Shows a license text as it is, in the mono font.
void HelpWindow::showLicenseFile(const std::filesystem::path& file)
{
    QFile in(QString::fromStdWString(file.wstring()));
    const QString text = in.open(QIODevice::ReadOnly) ? QString::fromUtf8(in.readAll()) : tr("The license text could not be read.");
    title_->setText(QString::fromStdWString(file.filename().wstring()));
    page_->setPlainText(text);
    page_->document()->setDefaultFont(style_.monoFont);
    page_->setFont(style_.monoFont);
    stack_->setCurrentIndex(kPageIndex);
    page_->verticalScrollBar()->setValue(0);
    headings_.clear();
    pages_->clearSelection();
}

// Finds the heading with this anchor and scrolls it to the top. The Markdown view has no anchors of its
// own, so headings are matched by their order in the page.
void HelpWindow::scrollToAnchor(const QString& anchor)
{
    int wanted = -1;
    for (std::size_t i = 0; i < headings_.size(); ++i) {
        if (fromCore(headings_[i].anchor) == anchor) wanted = static_cast<int>(i);
    }
    if (wanted < 0) return;
    int seen = 0;
    for (QTextBlock block = page_->document()->begin(); block.isValid(); block = block.next()) {
        if (block.blockFormat().headingLevel() <= 0) continue;
        if (seen++ != wanted) continue;
        QTextCursor cursor(block);
        page_->setTextCursor(cursor);
        const QRect at = page_->cursorRect(cursor);
        page_->verticalScrollBar()->setValue(page_->verticalScrollBar()->value() + at.top() - style_.unit * 2);
        return;
    }
}

// Opens a link through core: another page, a license text, or a web address in the browser.
void HelpWindow::followLink(const QUrl& url)
{
    const QString from = stack_->currentIndex() == kAboutIndex ? QStringLiteral("about") : current_.page;
    const plynth::HelpLink link = plynth::resolveHelpLink(docs_, licenses_, toCore(from), toCore(url.toString()));
    if (!link.status.ok()) return;
    if (!link.url.empty()) {
        QDesktopServices::openUrl(QUrl(fromCore(link.url)));
    } else if (!link.licenseFile.empty()) {
        go(Place{QString(), QString(), link.licenseFile}, true);
    } else {
        go(Place{fromCore(link.page), fromCore(link.anchor), {}}, true);
    }
}

// Lists the hits of the search, or goes back to the page when the box is empty.
void HelpWindow::runSearch()
{
    const QString text = search_->text().trimmed();
    if (text.isEmpty()) {
        if (stack_->currentIndex() == kResultsIndex) stack_->setCurrentIndex(kPageIndex);
        return;
    }
    results_->clear();
    const std::vector<plynth::HelpHit> hits = plynth::searchHelp(docs_, toCore(text));
    for (const plynth::HelpHit& hit : hits) {
        const QString where = hit.heading.empty() ? fromCore(hit.title) : fromCore(hit.title) + QStringLiteral(", ") + fromCore(hit.heading);
        auto* item = new QListWidgetItem(where + QStringLiteral("\n") + fromCore(hit.snippet), results_);
        item->setData(Qt::UserRole, fromCore(hit.page));
        item->setData(Qt::UserRole + 1, fromCore(hit.anchor));
        item->setToolTip(tr("Open %1 at this place").arg(fromCore(hit.title)));
    }
    if (hits.empty()) {
        auto* item = new QListWidgetItem(tr("Nothing found for \"%1\".").arg(text), results_);
        item->setFlags(Qt::NoItemFlags);
    }
    title_->setText(tr("Search results"));
    stack_->setCurrentIndex(kResultsIndex);
}

// Turns Back and Forward on or off for where the history stands.
void HelpWindow::updateButtons()
{
    back_->setEnabled(historyIndex_ > 0);
    forward_->setEnabled(historyIndex_ + 1 < static_cast<int>(history_.size()));
}

}
