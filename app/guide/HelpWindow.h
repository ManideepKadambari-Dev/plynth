#pragma once

#include "GuideStyle.h"

#include <plynth/core/HelpPages.h>

#include <QPixmap>
#include <QUrl>
#include <QWidget>

#include <filesystem>
#include <string>
#include <vector>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTextBrowser;
class QTimer;

namespace plynth::guide {

class AboutPage;

// The Help window: the list of help pages, a search across all of them, and the page itself. Pages come
// from docs/ and license texts from licenses/ through core, and nothing outside those two folders is read.
class HelpWindow : public QWidget {
    Q_OBJECT
public:
    HelpWindow(const GuideStyle& style, const std::filesystem::path& docs, const std::filesystem::path& licenses,
               const QPixmap& logo, QWidget* parent = nullptr);

    // Shows a page by id, scrolled to the heading with this anchor when one is given.
    void showPage(const QString& id, const QString& anchor = {});

    // Puts text in the search box and shows what it finds.
    void showSearch(const QString& text);

    // The id of the page shown now, empty when it is a license text or the search results.
    QString currentPage() const { return current_.page; }

signals:
    void setupCheckRequested();

private:
    // One place in the history: a page and anchor, or a license file.
    struct Place {
        QString page;
        QString anchor;
        std::filesystem::path licenseFile;
        bool operator==(const Place&) const = default;
    };

    void go(const Place& place, bool remember);
    void showLicenseFile(const std::filesystem::path& file);
    void scrollToAnchor(const QString& anchor);
    void followLink(const QUrl& url);
    void runSearch();
    void updateButtons();

    GuideStyle style_;
    std::filesystem::path docs_;
    std::filesystem::path licenses_;
    QLineEdit* search_ = nullptr;
    QListWidget* pages_ = nullptr;
    QPushButton* back_ = nullptr;
    QPushButton* forward_ = nullptr;
    QLabel* title_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QTextBrowser* page_ = nullptr;
    QListWidget* results_ = nullptr;
    AboutPage* about_ = nullptr;
    QTimer* searchTimer_ = nullptr;
    std::vector<plynth::HelpHeading> headings_;
    std::vector<Place> history_;
    int historyIndex_ = -1;
    Place current_;
};

}
