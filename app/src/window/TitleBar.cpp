#include "TitleBar.h"

#include "theme/Theme.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QtMath>
#include <QWindow>

namespace plynth::app {

TitleButton::TitleButton(Kind kind, QWidget* parent)
    : QAbstractButton(parent), kind_(kind)
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
}

// Switches the icon, used by the maximize button when the window is maximized or restored.
void TitleButton::setKind(Kind kind)
{
    kind_ = kind;
    update();
}

// Shows the hover look. The native maximize button hover comes from window messages.
void TitleButton::setHovered(bool hovered)
{
    if (hovered_ == hovered)
        return;
    hovered_ = hovered;
    update();
}

// Shows the pressed look. Used when Windows owns the mouse input of the button.
void TitleButton::setPressedLook(bool pressed)
{
    if (pressedLook_ == pressed)
        return;
    pressedLook_ = pressed;
    update();
}

QSize TitleButton::sizeHint() const
{
    const Theme& t = Theme::instance();
    return QSize(t.titleButtonWidth(), t.titleBarHeight());
}

// Draws the hover background and the icon lines in the theme text color.
void TitleButton::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const bool lit = hovered_ || underMouse() || pressedLook_ || isDown();
    if (lit)
        p.fillRect(rect(), kind_ == Kind::Close ? t.line() : t.raised());

    const qreal ratio = devicePixelRatioF();
    QPen pen(t.text());
    pen.setWidthF(1.0);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    p.setPen(pen);

    // Puts a coordinate on the middle of a physical pixel so straight lines stay sharp.
    auto snap = [ratio](qreal v) { return (qFloor(v * ratio) + 0.5) / ratio; };

    const int size = t.unit() * 3;
    const qreal x = snap((width() - size) / 2.0);
    const qreal y = snap((height() - size) / 2.0);
    const qreal s = size;
    switch (kind_) {
    case Kind::Minimize:
        p.drawLine(QPointF(x, snap(y + s / 2)), QPointF(x + s, snap(y + s / 2)));
        break;
    case Kind::Maximize:
        p.drawRect(QRectF(x, y, s, s));
        break;
    case Kind::Restore: {
        const qreal off = snap(t.unit() / 2.0 + 1) - snap(0);
        p.drawRect(QRectF(x, y + off, s - off, s - off));
        QPolygonF back;
        back << QPointF(x + off, y + off) << QPointF(x + off, y) << QPointF(x + s, y)
             << QPointF(x + s, y + s - off) << QPointF(x + s - off, y + s - off);
        p.drawPolyline(back);
        break;
    }
    case Kind::Close:
        p.drawLine(QPointF(x, y), QPointF(x + s, y + s));
        p.drawLine(QPointF(x + s, y), QPointF(x, y + s));
        break;
    }
}

void TitleButton::enterEvent(QEnterEvent* event)
{
    QAbstractButton::enterEvent(event);
    update();
}

void TitleButton::leaveEvent(QEvent* event)
{
    QAbstractButton::leaveEvent(event);
    update();
}

TitleBar::TitleBar(QWidget* parent)
    : QWidget(parent)
{
    const Theme& t = Theme::instance();
    setFixedHeight(t.titleBarHeight());
    setAutoFillBackground(false);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(t.unit() * 3, 0, 0, 0);
    layout->setSpacing(0);

    auto* logo = new QLabel(this);
    const qreal ratio = devicePixelRatioF();
    QPixmap logoPixmap = QPixmap(QStringLiteral(":/logo.png"))
                             .scaled(QSize(t.logoSize(), t.logoSize()) * ratio,
                                     Qt::KeepAspectRatio, Qt::SmoothTransformation);
    logoPixmap.setDevicePixelRatio(ratio);
    logo->setPixmap(logoPixmap);
    logo->setFixedSize(t.logoSize(), t.logoSize());
    logo->setToolTip(tr("Plynth"));
    logo_ = logo;
    layout->addWidget(logo);
    layout->addSpacing(t.unit() * 2);

    menuBar_ = new QMenuBar(this);
    menuBar_->setNativeMenuBar(false);
    menuBar_->setFont(t.uiFont(t.sizeNormal()));
    fileMenu_ = menuBar_->addMenu(tr("&File"));
    menuBar_->addMenu(tr("&Edit"));
    menuBar_->addMenu(tr("&View"));
    menuBar_->addMenu(tr("&Build"));
    menuBar_->addMenu(tr("&Catalog"));
    helpMenu_ = menuBar_->addMenu(tr("&Help"));
    menuBar_->setToolTip(tr("Menu"));
    menuBar_->setProperty("tourId", QStringLiteral("menu"));
    layout->addWidget(menuBar_, 0, Qt::AlignVCenter);
    layout->addStretch(1);

    minimizeButton_ = new TitleButton(TitleButton::Kind::Minimize, this);
    maximizeButton_ = new TitleButton(TitleButton::Kind::Maximize, this);
    closeButton_ = new TitleButton(TitleButton::Kind::Close, this);
    minimizeButton_->setToolTip(tr("Minimize"));
    maximizeButton_->setToolTip(tr("Maximize"));
    closeButton_->setToolTip(tr("Close (Alt+F4)"));
    minimizeButton_->setProperty("tourId", QStringLiteral("title-minimize"));
    maximizeButton_->setProperty("tourId", QStringLiteral("title-maximize"));
    closeButton_->setProperty("tourId", QStringLiteral("title-close"));
    for (TitleButton* b : {minimizeButton_, maximizeButton_, closeButton_}) {
        b->setFixedSize(b->sizeHint());
        layout->addWidget(b);
    }

    connect(minimizeButton_, &QAbstractButton::clicked, this, &TitleBar::minimizeRequested);
    connect(maximizeButton_, &QAbstractButton::clicked, this, &TitleBar::maximizeToggleRequested);
    connect(closeButton_, &QAbstractButton::clicked, this, &TitleBar::closeRequested);
}

void TitleBar::setProjectName(const QString& name)
{
    projectName_ = name;
    update();
}

// Switches the maximize button between its maximize and restore icon and tooltip.
void TitleBar::setMaximized(bool maximized)
{
    maximizeButton_->setKind(maximized ? TitleButton::Kind::Restore : TitleButton::Kind::Maximize);
    maximizeButton_->setToolTip(maximized ? tr("Restore") : tr("Maximize"));
}

// Tells the native window code what is under a point of the title bar.
TitleBar::Hit TitleBar::hitTest(const QPoint& localPos) const
{
    QWidget* child = childAt(localPos);
    if (child == nullptr || child == this)
        return Hit::Caption;
    if (child == maximizeButton_)
        return Hit::MaximizeButton;
    return Hit::Client;
}

// Paints the Panel background, a bottom line and the project name centred in the bar.
void TitleBar::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.fillRect(rect(), t.panel());
    p.setPen(t.line());
    p.drawLine(0, height() - 1, width(), height() - 1);

    const int left = menuBar_->geometry().right() + t.unit() * 4;
    const int right = minimizeButton_->geometry().left() - t.unit() * 4;
    const int half = qMin(width() / 2 - left, right - width() / 2);
    if (half <= 0)
        return;
    const QRect box(width() / 2 - half, 0, half * 2, height() - 1);
    p.setFont(t.uiFont(t.sizeNormal()));
    p.setPen(t.muted());
    p.drawText(box, Qt::AlignCenter,
               p.fontMetrics().elidedText(projectName_, Qt::ElideRight, box.width()));
}

// Starts a window move when the empty bar is dragged.
void TitleBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && window()->windowHandle() != nullptr)
        window()->windowHandle()->startSystemMove();
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit maximizeToggleRequested();
}

}
