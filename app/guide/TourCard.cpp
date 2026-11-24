#include "TourCard.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace plynth::guide {

namespace {

// A button of the card with its tooltip and tour id.
QPushButton* cardButton(const QString& text, const QString& tip, const QString& tourId, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setToolTip(tip);
    button->setProperty("tourId", tourId);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

}

// Builds the labels and, for the tour, the three buttons.
TourCard::TourCard(const GuideStyle& style, bool withButtons, QWidget* parent) : QFrame(parent), style_(style)
{
    setObjectName(QStringLiteral("tourCard"));
    setProperty("tourId", withButtons ? QStringLiteral("tour-card") : QStringLiteral("what-is-this-card"));
    setStyleSheet(guideStyleSheet(style));
    setFixedWidth(style.cardWidth);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(style.unit * 4, style.unit * 3, style.unit * 4, style.unit * 3);
    column->setSpacing(style.unit * 2);

    name_ = new QLabel(this);
    name_->setFont(style.titleFont);
    name_->setWordWrap(true);
    column->addWidget(name_);

    text_ = new QLabel(this);
    text_->setFont(style.uiFont);
    text_->setWordWrap(true);
    column->addWidget(text_);

    shortcut_ = new QLabel(this);
    shortcut_->setFont(style.monoFont);
    shortcut_->setProperty("muted", true);
    shortcut_->setToolTip(tr("The keyboard shortcut of this control"));
    column->addWidget(shortcut_);

    if (!withButtons) return;

    auto* row = new QHBoxLayout;
    row->setSpacing(style.unit * 2);
    counter_ = new QLabel(this);
    counter_->setFont(style.monoFont);
    counter_->setProperty("muted", true);
    counter_->setToolTip(tr("Which step of the tour this is"));
    row->addWidget(counter_);
    row->addStretch(1);
    skip_ = cardButton(tr("Skip"), tr("End the tour (Esc). Nothing in your project changes"), QStringLiteral("tour-skip"), this);
    back_ = cardButton(tr("Back"), tr("Go back to the step before (Left)"), QStringLiteral("tour-back"), this);
    next_ = cardButton(tr("Next"), tr("Go on to the next step (Right or Enter)"), QStringLiteral("tour-next"), this);
    next_->setProperty("primary", true);
    next_->setDefault(true);
    row->addWidget(skip_);
    row->addWidget(back_);
    row->addWidget(next_);
    column->addLayout(row);
    connect(back_, &QPushButton::clicked, this, &TourCard::backClicked);
    connect(next_, &QPushButton::clicked, this, &TourCard::nextClicked);
    connect(skip_, &QPushButton::clicked, this, &TourCard::skipClicked);
}

// Fills in the three lines of text.
void TourCard::setContent(const QString& name, const QString& text, const QString& shortcut)
{
    name_->setText(name);
    text_->setText(text);
    shortcut_->setText(shortcut);
    shortcut_->setVisible(!shortcut.isEmpty());
    adjustSize();
}

// Shows the step number and sets the buttons for the first and last step.
void TourCard::setStep(int number, int count, bool canGoBack, bool isLast)
{
    if (!counter_) return;
    counter_->setText(tr("%1 of %2").arg(number).arg(count));
    back_->setEnabled(canGoBack);
    next_->setText(isLast ? tr("Done") : tr("Next"));
    next_->setToolTip(isLast ? tr("End the tour (Enter)") : tr("Go on to the next step (Right or Enter)"));
    adjustSize();
}

// The height the content needs at the fixed width.
QSize TourCard::cardSize() const
{
    const int height = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(width()) : sizeHint().height();
    return {width(), std::max(height, sizeHint().height())};
}

// Tries each side of the target in turn and keeps the first where the card fits.
QRect TourCard::placeCard(const QRect& area, const QRect& target, const QSize& card, int gap)
{
    const QRect inner = area.adjusted(gap, gap, -gap, -gap);
    const auto clampX = [&](int x) { return std::clamp(x, inner.left(), std::max(inner.left(), inner.right() + 1 - card.width())); };
    const auto clampY = [&](int y) { return std::clamp(y, inner.top(), std::max(inner.top(), inner.bottom() + 1 - card.height())); };

    if (target.bottom() + gap + card.height() <= inner.bottom() + 1) {
        return {QPoint(clampX(target.left()), target.bottom() + 1 + gap), card};
    }
    if (target.top() - gap - card.height() >= inner.top()) {
        return {QPoint(clampX(target.left()), target.top() - gap - card.height()), card};
    }
    if (target.right() + gap + card.width() <= inner.right() + 1) {
        return {QPoint(target.right() + 1 + gap, clampY(target.top())), card};
    }
    if (target.left() - gap - card.width() >= inner.left()) {
        return {QPoint(target.left() - gap - card.width(), clampY(target.top())), card};
    }
    return {QPoint(clampX(inner.center().x() - card.width() / 2), clampY(inner.bottom() + 1 - card.height())), card};
}

}
