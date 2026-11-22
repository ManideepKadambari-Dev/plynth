#include "MaterialRow.h"

#include "theme/Theme.h"

#include <QPainter>

namespace plynth::app {

MaterialRow::MaterialRow(QWidget* parent) : QAbstractButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

// Stores the content and asks for a new size, since a note makes the row two lines tall.
void MaterialRow::setContent(const QPixmap& swatch, const QString& name, const QString& note, const QString& warning)
{
    swatch_ = swatch;
    name_ = name;
    note_ = note;
    warning_ = warning;
    updateGeometry();
    update();
}

// One line tall without a note, two lines with one.
QSize MaterialRow::sizeHint() const
{
    const Theme& t = Theme::instance();
    const bool second = !note_.isEmpty() || !warning_.isEmpty();
    const int height = qMax(t.materialSwatch() + t.unit() * 2, second ? t.unit() * 10 : 0);
    return QSize(t.unit() * 20, height + (second ? t.unit() : 0));
}

void MaterialRow::enterEvent(QEnterEvent* event)
{
    hover_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void MaterialRow::leaveEvent(QEvent* event)
{
    hover_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

// Paints a Raised box with a Line border, the swatch, the name and the note.
void MaterialRow::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const bool enabled = isEnabled();
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setBrush(t.raised());
    p.setPen(QPen(hover_ && enabled ? t.muted() : t.line(), 1));
    p.drawRoundedRect(box, t.radius(), t.radius());

    const int pad = t.unit() * 2;
    const int swatch = t.materialSwatch();
    const QRect swatchRect(pad, (height() - swatch) / 2, swatch, swatch);
    p.setBrush(t.ground());
    p.setPen(QPen(t.line(), 1));
    p.drawRoundedRect(QRectF(swatchRect).adjusted(0.5, 0.5, -0.5, -0.5), t.radius() / 2.0, t.radius() / 2.0);
    if (!swatch_.isNull()) {
        p.save();
        p.setClipRect(swatchRect.adjusted(1, 1, -1, -1));
        p.drawPixmap(swatchRect.adjusted(1, 1, -1, -1), swatch_);
        p.restore();
    }

    const int textLeft = swatchRect.right() + pad + 1;
    const int textWidth = qMax(0, width() - textLeft - pad);
    const bool second = !note_.isEmpty() || !warning_.isEmpty();
    const QColor nameColor = enabled ? t.text() : t.muted();
    p.setFont(t.uiFont(t.sizeNormal()));
    p.setPen(nameColor);
    const QRect nameRect(textLeft, second ? t.unit() : 0, textWidth, second ? t.unit() * 5 : height());
    p.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(name_, Qt::ElideRight, textWidth));
    if (second) {
        p.setFont(t.uiFont(t.sizeSmall()));
        p.setPen(warning_.isEmpty() ? t.muted() : t.signalAmber());
        const QString text = warning_.isEmpty() ? note_ : warning_;
        p.drawText(QRect(textLeft, t.unit() * 6, textWidth, t.unit() * 4), Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(text, Qt::ElideRight, textWidth));
    }
}

}
