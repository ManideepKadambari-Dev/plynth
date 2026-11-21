#pragma once

#include <QLabel>
#include <QPainter>

namespace plynth::app {

// A one line label that cuts the middle out of a long text so it never makes its row wider. Put the full text
// in the tooltip.
class ElideLabel : public QLabel {
public:
    using QLabel::QLabel;

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
