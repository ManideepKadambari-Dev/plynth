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

}
