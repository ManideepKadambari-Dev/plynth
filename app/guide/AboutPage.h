#pragma once

#include "GuideStyle.h"

#include <QPixmap>
#include <QUrl>
#include <QWidget>

#include <filesystem>

class QListWidget;
class QTextBrowser;

namespace plynth::guide {

// The About page: logo, version, the licenses of Plynth and its samples, the copyright line, the third
// party notices and the list of license texts.
class AboutPage : public QWidget {
    Q_OBJECT
public:
    AboutPage(const GuideStyle& style, const QPixmap& logo, const std::filesystem::path& licensesFolder,
              QWidget* parent = nullptr);

signals:
    // A link in the notices was clicked. The Help window resolves it.
    void linkClicked(const QUrl& url);
    // A license text in the list was picked.
    void licenseFileRequested(const std::filesystem::path& file);

private:
    QTextBrowser* notices_ = nullptr;
    QListWidget* texts_ = nullptr;
};

}
