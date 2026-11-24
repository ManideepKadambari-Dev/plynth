#pragma once

#include <plynth/core/HelpPages.h>

#include <QFont>
#include <QString>
#include <QTextDocument>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace plynth::guide {

// The fonts and margin a help page is shown with.
struct HelpPageLook {
    QFont text;
    QFont mono;
    qreal margin = 0;
};

// One help page made ready to show: its document and headings. ok is false when the page could not be read,
// and the document then holds the reason.
struct RenderedHelpPage {
    std::unique_ptr<QTextDocument> document;
    std::vector<plynth::HelpHeading> headings;
    bool ok = false;
};

// Turns Markdown into a document. The document belongs to no view while it is built, so nothing is laid out
// until it is shown, and it can be built away from the main thread.
std::unique_ptr<QTextDocument> renderHelpMarkdown(const QString& markdown, const HelpPageLook& look);

// Turns plain text, such as a license, into a document in the mono font.
std::unique_ptr<QTextDocument> renderHelpPlainText(const QString& text, const HelpPageLook& look);

// Reads a help page through core and turns it into a document.
RenderedHelpPage renderHelpPage(const std::filesystem::path& docs, const std::string& id, const HelpPageLook& look);

}
