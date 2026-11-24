#include "HelpRender.h"

#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextFragment>

#include <utility>
#include <vector>

namespace plynth::guide {

namespace {

// A new empty document in the page fonts, with no undo history to keep.
std::unique_ptr<QTextDocument> emptyDocument(const QFont& font, qreal margin)
{
    auto document = std::make_unique<QTextDocument>();
    document->setUndoRedoEnabled(false);
    document->setDefaultFont(font);
    document->setDocumentMargin(margin);
    return document;
}

// Sets every code span and code block in the mono font. The places are found first and then changed in one
// edit, because changing text while walking it, or one change at a time, is slow on long pages.
void monoForCode(QTextDocument* document, const QFont& mono)
{
    std::vector<std::pair<int, int>> spans;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().fontFixedPitch()) continue;
            const int start = fragment.position();
            const int end = start + fragment.length();
            if (!spans.empty() && spans.back().second == start) spans.back().second = end;
            else spans.emplace_back(start, end);
        }
    }
    if (spans.empty()) return;
    QTextCharFormat format;
    format.setFontFamilies({mono.family()});
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    for (const auto& [start, end] : spans) {
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(format);
    }
    cursor.endEditBlock();
}

}

// Builds the document of a Markdown page.
std::unique_ptr<QTextDocument> renderHelpMarkdown(const QString& markdown, const HelpPageLook& look)
{
    auto document = emptyDocument(look.text, look.margin);
    document->setMarkdown(markdown, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub |
                                                                    QTextDocument::MarkdownNoHTML));
    monoForCode(document.get(), look.mono);
    return document;
}

// Builds the document of a plain text.
std::unique_ptr<QTextDocument> renderHelpPlainText(const QString& text, const HelpPageLook& look)
{
    auto document = emptyDocument(look.mono, look.margin);
    document->setPlainText(text);
    QTextCharFormat format;
    format.setFontFamilies({look.mono.family()});
    format.setFontFixedPitch(true);
    QTextCursor cursor(document.get());
    cursor.select(QTextCursor::Document);
    cursor.mergeCharFormat(format);
    return document;
}

// Reads a page and builds its document, or a document with the reason it could not be read.
RenderedHelpPage renderHelpPage(const std::filesystem::path& docs, const std::string& id, const HelpPageLook& look)
{
    RenderedHelpPage out;
    const plynth::HelpPageRead read = plynth::readHelpPage(docs, id);
    if (!read.status.ok()) {
        out.document = emptyDocument(look.text, look.margin);
        out.document->setPlainText(QString::fromStdString(read.status.message));
        return out;
    }
    out.document = renderHelpMarkdown(
        QString::fromUtf8(read.markdown.data(), static_cast<qsizetype>(read.markdown.size())), look);
    out.headings = read.headings;
    out.ok = true;
    return out;
}

}
