#pragma once

#include <QLabel>
#include <QPainter>

namespace plynth::app {

// A one line label that cuts the middle out of a long text so it never makes its row wider. Put the full text
// in the tooltip. The text is always plain, so tags in a name show as typed.
class ElideLabel : public QLabel {
public:
    // Makes an empty plain text label.
    explicit ElideLabel(QWidget* parent = nullptr, Qt::WindowFlags f = {}) : QLabel(parent, f) { setTextFormat(Qt::PlainText); }

    // Makes a plain text label that shows the given text.
    explicit ElideLabel(const QString& text, QWidget* parent = nullptr, Qt::WindowFlags f = {}) : ElideLabel(parent, f)
    {
        setText(text);
    }

    // Lets the layout shrink the label to nothing.
    QSize minimumSizeHint() const override { return QSize(0, QLabel::minimumSizeHint().height()); }

protected:
    // Draws the text shortened to the width, in the label's own font and color.
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setFont(font());
        p.setPen(palette().color(foregroundRole()));
        p.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(text(), Qt::ElideMiddle, width()));
    }
};

}
