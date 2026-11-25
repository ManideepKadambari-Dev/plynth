#pragma once

#include <QString>
#include <QTextDocument>

namespace plynth::app {

// Turns any text into a tooltip whose tags show as typed. Qt reads a tooltip with a tag in its first line as
// rich text and would load pictures it names, so every name from a zip, a store or Blender goes through here.
inline QString plainToolTip(const QString& text)
{
    if (text.isEmpty())
        return {};
    return Qt::convertFromPlainText(text, Qt::WhiteSpaceNormal);
}

// Lets a wrapping label break a long path or id at its separators, so one long word cannot make a pane wider.
// An invisible zero width space follows each slash, backslash, underscore and hyphen.
inline QString breakable(const QString& text)
{
    QString out;
    out.reserve(text.size() + text.size() / 8);
    for (const QChar c : text) {
        out += c;
        if (c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char('_') || c == QLatin1Char('-'))
            out += QChar(0x200B);
    }
    return out;
}

}
