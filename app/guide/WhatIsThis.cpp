#include "WhatIsThis.h"

#include "TourCard.h"
#include "TourHost.h"

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>

#include <functional>

namespace plynth::guide {

namespace {

// Text from core as a Qt string.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// A see-through layer over the window that takes every click, so no control under it runs.
class Catcher : public QWidget {
public:
    Catcher(const GuideStyle& style, QWidget* window) : QWidget(window), style_(style)
    {
        setProperty("tourId", QStringLiteral("what-is-this-layer"));
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setCursor(Qt::WhatsThisCursor);
        setFocusPolicy(Qt::StrongFocus);
        setGeometry(window->rect());
    }

    std::function<void(const QPoint&)> clicked;
    std::function<void()> escaped;
    QRect outline;

protected:
    // Draws the amber outline around the control being explained.
    void paintEvent(QPaintEvent*) override
    {
        if (outline.isEmpty()) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPen pen(style_.amber);
        pen.setWidth(style_.outlineWidth);
        painter.setPen(pen);
        painter.drawRoundedRect(outline, style_.radius, style_.radius);
    }

    // Passes the point of a click on.
    void mousePressEvent(QMouseEvent* event) override
    {
        event->accept();
        if (clicked) clicked(event->position().toPoint());
    }

    // Swallows the release and double click, so nothing under the layer sees them.
    void mouseReleaseEvent(QMouseEvent* event) override { event->accept(); }
    void mouseDoubleClickEvent(QMouseEvent* event) override { event->accept(); }

    // Esc ends the mode. Other keys do nothing.
    void keyPressEvent(QKeyEvent* event) override
    {
        event->accept();
        if (event->key() == Qt::Key_Escape && escaped) escaped();
    }

private:
    GuideStyle style_;
};

}

// Keeps a copy of the tour data.
WhatIsThis::WhatIsThis(const plynth::TourData& data, TourHost* host, const GuideStyle& style, QObject* parent)
    : QObject(parent), data_(data), host_(host), style_(style)
{
}

// Ends the mode, so the layer and the filter go away with the object.
WhatIsThis::~WhatIsThis()
{
    setActive(false);
}

// Puts the catching layer over the window and watches menus, or takes both away.
void WhatIsThis::setActive(bool active)
{
    if (active == active_) return;
    active_ = active;
    if (active) {
        QWidget* window = host_ ? host_->window() : nullptr;
        if (!window) {
            active_ = false;
            return;
        }
        auto* catcher = new Catcher(style_, window);
        catcher->clicked = [this](const QPoint& p) { clickAt(p); };
        catcher->escaped = [this] { setActive(false); };
        catcher_ = catcher;
        window->installEventFilter(this);
        qApp->installEventFilter(this);
        catcher->show();
        catcher->raise();
        catcher->setFocus();
    } else {
        qApp->removeEventFilter(this);
        if (host_ && host_->window()) host_->window()->removeEventFilter(this);
        if (card_) card_->deleteLater();
        if (catcher_) catcher_->deleteLater();
        card_ = nullptr;
        catcher_ = nullptr;
    }
    emit activeChanged(active_);
}

// Explains a control found through the host.
bool WhatIsThis::explain(const QString& controlId)
{
    if (!active_) setActive(true);
    if (!active_ || !host_) return false;
    QWidget* control = host_->findControl(controlId);
    if (!control) return false;
    QWidget* window = host_->window();
    const QRect area(window->mapFromGlobal(control->mapToGlobal(QPoint(0, 0))), control->size());
    showCard(controlId, area);
    return true;
}

// Takes clicks on menus while the mode is on, follows the window size and ends the mode on Esc.
bool WhatIsThis::eventFilter(QObject* watched, QEvent* event)
{
    if (!active_) return QObject::eventFilter(watched, event);
    if (host_ && watched == host_->window() && event->type() == QEvent::Resize && catcher_) {
        catcher_->setGeometry(host_->window()->rect());
        return false;
    }
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        if (!QApplication::activePopupWidget()) {
            setActive(false);
            return true;
        }
        return false;
    }
    auto* menu = qobject_cast<QMenu*>(watched);
    if (!menu) return QObject::eventFilter(watched, event);
    if (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::MouseButtonDblClick) return true;
    if (event->type() != QEvent::MouseButtonPress) return QObject::eventFilter(watched, event);

    const QPoint point = static_cast<QMouseEvent*>(event)->position().toPoint();
    QAction* action = menu->actionAt(point);
    if (!action || action->menu()) return false;
    const QString id = action->property("tourId").toString();
    QWidget* window = host_->window();
    const QRect area(window->mapFromGlobal(menu->mapToGlobal(menu->actionGeometry(action).topLeft())),
                     menu->actionGeometry(action).size());
    while (QWidget* popup = QApplication::activePopupWidget()) popup->close();
    showCard(id, area);
    return true;
}

// Finds the control under a click and explains it. A click on the menu bar opens that menu instead.
void WhatIsThis::clickAt(const QPoint& windowPoint)
{
    QWidget* window = host_->window();
    if (!catcher_ || !window) return;
    catcher_->hide();
    QWidget* under = window->childAt(windowPoint);
    catcher_->show();
    catcher_->raise();
    if (card_ && card_->isVisible() && card_->geometry().contains(windowPoint)) return;
    if (auto* bar = qobject_cast<QMenuBar*>(under)) {
        QAction* action = bar->actionAt(bar->mapFrom(window, windowPoint));
        if (action && action->menu()) {
            bar->setActiveAction(action);
            return;
        }
    }
    for (QWidget* w = under; w && w != window; w = w->parentWidget()) {
        const QString id = idOf(w);
        if (id.isEmpty()) continue;
        QWidget* owner = w;
        while (owner->parentWidget() && owner->property("tourId").toString().isEmpty()) owner = owner->parentWidget();
        const QRect area(owner->mapTo(window, QPoint(0, 0)), owner->size());
        showCard(id, area);
        return;
    }
    showCard(QString(), QRect(windowPoint, QSize(1, 1)));
}

// The first tour id on the widget or its parents.
QString WhatIsThis::idOf(QWidget* widget)
{
    for (QWidget* w = widget; w; w = w->parentWidget()) {
        const QString id = w->property("tourId").toString();
        if (!id.isEmpty()) return id;
    }
    return {};
}

// Fills the card with the entry and puts it next to the area, with the area outlined.
void WhatIsThis::showCard(const QString& id, const QRect& area)
{
    auto* catcher = static_cast<Catcher*>(catcher_.data());
    if (!catcher) return;
    if (!card_) card_ = new TourCard(style_, false, catcher);
    const plynth::TourEntry* entry = id.isEmpty() ? nullptr : plynth::tourEntry(data_, id.toStdString());
    if (entry) {
        card_->setContent(fromCore(entry->name), fromCore(entry->text), host_->shortcutText(id));
    } else {
        card_->setContent(tr("No explanation"), tr("This part of the window has no explanation yet."), QString());
    }
    catcher->outline = entry ? area.adjusted(-style_.unit, -style_.unit, style_.unit, style_.unit) : QRect();
    outline_ = catcher->outline;
    card_->setGeometry(TourCard::placeCard(catcher->rect(), area, card_->cardSize(), style_.unit * 3));
    card_->show();
    card_->raise();
    catcher->update();
    catcher->setFocus();
}

}
