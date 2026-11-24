#pragma once

#include "GuideStyle.h"

#include <QFrame>
#include <QRect>
#include <QSize>

class QLabel;
class QPushButton;

namespace plynth::guide {

// The card that names a control and says what it does. The tour shows Back, Next and Skip on it.
class TourCard : public QFrame {
    Q_OBJECT
public:
    TourCard(const GuideStyle& style, bool withButtons, QWidget* parent = nullptr);

    // Sets the name, the explanation and the shortcut. An empty shortcut hides its line.
    void setContent(const QString& name, const QString& text, const QString& shortcut);

    // Shows "3 of 12" and sets the Back and Next buttons for that step.
    void setStep(int number, int count, bool canGoBack, bool isLast);

    // The size of the card at the style's card width.
    QSize cardSize() const;

    // Where a card of this size goes next to the target, always inside area: below, above, right, left,
    // else at the bottom middle.
    static QRect placeCard(const QRect& area, const QRect& target, const QSize& card, int gap);

signals:
    void backClicked();
    void nextClicked();
    void skipClicked();

private:
    GuideStyle style_;
    QLabel* name_ = nullptr;
    QLabel* text_ = nullptr;
    QLabel* shortcut_ = nullptr;
    QLabel* counter_ = nullptr;
    QPushButton* back_ = nullptr;
    QPushButton* next_ = nullptr;
    QPushButton* skip_ = nullptr;
};

}
