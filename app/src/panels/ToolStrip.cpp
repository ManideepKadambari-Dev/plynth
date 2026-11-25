#include "ToolStrip.h"

#include "theme/Theme.h"
#include "tools/ToolController.h"

#include <QPainter>
#include <QPolygonF>
#include <QVBoxLayout>

namespace plynth::app {

namespace {

constexpr int kBorderWidth = 1;

}

// Makes a checkable button for one tool.
ToolButton::ToolButton(ToolId tool, QWidget* parent)
    : QAbstractButton(parent), tool_(tool)
{
    setCheckable(true);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
}

// The square size from the theme.
QSize ToolButton::sizeHint() const
{
    const int side = Theme::instance().toolButton();
    return QSize(side, side);
}

// Paints the button background and the icon of its tool.
void ToolButton::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const bool active = isChecked();
    QColor ink = t.text();
    if (active) {
        p.setPen(Qt::NoPen);
        p.setBrush(t.signalBlue());
        p.drawRoundedRect(rect(), t.radius(), t.radius());
        ink = t.onBlue();
    } else if (hovered_ && isEnabled()) {
        p.setPen(Qt::NoPen);
        p.setBrush(t.raised());
        p.drawRoundedRect(rect(), t.radius(), t.radius());
    }
    if (!isEnabled())
        ink = t.muted();

    const QPointF c = QRectF(rect()).center();
    const double u = width() / 32.0;
    QPen pen(ink, 1.6 * u, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    switch (tool_) {
    case ToolId::Select: {
        const QPolygonF arrow({c + QPointF(-5, -8) * u, c + QPointF(-5, 8) * u, c + QPointF(-1.5, 4.5) * u,
                               c + QPointF(1.5, 10) * u, c + QPointF(4, 8.8) * u, c + QPointF(1.2, 3.5) * u,
                               c + QPointF(6, 3.5) * u});
        p.setBrush(ink);
        p.drawPolygon(arrow);
        break;
    }
    case ToolId::Scale: {
        const QPointF a = c + QPointF(-9, 5) * u;
        const QPointF b = c + QPointF(9, -5) * u;
        p.drawLine(a, b);
        const QPointF n = QPointF(0.5, 0.87) * 4.5 * u;
        p.drawLine(a - n, a + n);
        p.drawLine(b - n, b + n);
        break;
    }
    case ToolId::Wall: {
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        p.drawRect(QRectF(c + QPointF(-9, -2.5) * u, QSizeF(18, 5) * u));
        p.drawEllipse(c + QPointF(-9, 0) * u, 3.2 * u, 3.2 * u);
        p.drawEllipse(c + QPointF(9, 0) * u, 3.2 * u, 3.2 * u);
        break;
    }
    case ToolId::Opening: {
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        p.drawRect(QRectF(c + QPointF(-11, 4) * u, QSizeF(7, 4) * u));
        p.drawRect(QRectF(c + QPointF(6, 4) * u, QSizeF(5, 4) * u));
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawLine(c + QPointF(-4, 6) * u, c + QPointF(-4, -4) * u);
        p.drawArc(QRectF(c + QPointF(-14, -4) * u, QSizeF(20, 20) * u), 0, 90 * 16);
        break;
    }
    case ToolId::Room: {
        p.drawRect(QRectF(c + QPointF(-8, -8) * u, QSizeF(16, 16) * u));
        p.drawLine(c + QPointF(-8, 0) * u, c + QPointF(0, 0) * u);
        p.drawLine(c + QPointF(0, 0) * u, c + QPointF(0, 8) * u);
        break;
    }
    case ToolId::Stair: {
        QPolygonF steps;
        steps << c + QPointF(-10, 9) * u << c + QPointF(-10, 3) * u << c + QPointF(-4, 3) * u << c + QPointF(-4, -3) * u
              << c + QPointF(2, -3) * u << c + QPointF(2, -9) * u << c + QPointF(10, -9) * u;
        p.drawPolyline(steps);
        p.drawLine(c + QPointF(-10, 9) * u, c + QPointF(10, 9) * u);
        break;
    }
    case ToolId::Light: {
        const QPointF bulb = c + QPointF(0, -2) * u;
        p.drawEllipse(bulb, 5 * u, 5 * u);
        p.drawLine(c + QPointF(-2.5, 5) * u, c + QPointF(2.5, 5) * u);
        p.drawLine(c + QPointF(-2, 8) * u, c + QPointF(2, 8) * u);
        p.drawLine(bulb + QPointF(-9, 0) * u, bulb + QPointF(-7, 0) * u);
        p.drawLine(bulb + QPointF(7, 0) * u, bulb + QPointF(9, 0) * u);
        p.drawLine(bulb + QPointF(0, -9) * u, bulb + QPointF(0, -7) * u);
        break;
    }
    case ToolId::Furniture: {
        p.drawRoundedRect(QRectF(c + QPointF(-9, -7) * u, QSizeF(18, 9) * u), 2 * u, 2 * u);
        p.drawRoundedRect(QRectF(c + QPointF(-11, -1) * u, QSizeF(22, 8) * u), 2 * u, 2 * u);
        p.drawLine(c + QPointF(-7, 7) * u, c + QPointF(-7, 10) * u);
        p.drawLine(c + QPointF(7, 7) * u, c + QPointF(7, 10) * u);
        break;
    }
    case ToolId::Detect: {
        const QPointF lens = c + QPointF(-2.5, -2.5) * u;
        p.drawEllipse(lens, 6.5 * u, 6.5 * u);
        p.drawLine(c + QPointF(2.2, 2.2) * u, c + QPointF(9, 9) * u);
        p.drawLine(lens + QPointF(-3, 0) * u, lens + QPointF(3, 0) * u);
        p.drawLine(lens + QPointF(0, -3) * u, lens + QPointF(0, 3) * u);
        break;
    }
    }
}

// Shows the hover background.
void ToolButton::enterEvent(QEnterEvent* event)
{
    QAbstractButton::enterEvent(event);
    hovered_ = true;
    update();
}

// Removes the hover background.
void ToolButton::leaveEvent(QEvent* event)
{
    QAbstractButton::leaveEvent(event);
    hovered_ = false;
    update();
}

// Builds the tool buttons and connects them to the controller.
ToolStrip::ToolStrip(ToolController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller)
{
    const Theme& t = Theme::instance();
    setFixedWidth(t.toolStripWidth());

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins((t.toolStripWidth() - kBorderWidth - t.toolButton()) / 2, t.unit() * 2,
                               (t.toolStripWidth() - kBorderWidth - t.toolButton()) / 2 + kBorderWidth, t.unit() * 2);
    layout->setSpacing(t.unit());

    const std::array<ToolId, kToolCount>& ids = kStripTools;
    for (size_t i = 0; i < ids.size(); ++i) {
        auto* button = new ToolButton(ids[i], this);
        button->setFixedSize(button->sizeHint());
        connect(button, &QAbstractButton::clicked, this, [this, id = ids[i]]() {
            controller_->setActive(id);
            refresh();
        });
        layout->addWidget(button);
        buttons_[static_cast<size_t>(ids[i])] = button;
    }
    layout->addStretch(1);

    connect(controller_, &ToolController::activeChanged, this, &ToolStrip::refresh);
    connect(controller_, &ToolController::availabilityChanged, this, &ToolStrip::refresh);
    refresh();
}

// The plain name of a tool.
QString ToolStrip::toolName(ToolId id)
{
    switch (id) {
    case ToolId::Select: return tr("Select");
    case ToolId::Scale: return tr("Scale");
    case ToolId::Wall: return tr("Wall");
    case ToolId::Opening: return tr("Opening");
    case ToolId::Room: return tr("Room");
    case ToolId::Stair: return tr("Stair");
    case ToolId::Light: return tr("Light");
    case ToolId::Furniture: return tr("Furniture");
    case ToolId::Detect: return tr("Detect");
    }
    return QString();
}

// The single key that picks a tool.
QString ToolStrip::toolKey(ToolId id)
{
    switch (id) {
    case ToolId::Select: return QStringLiteral("V");
    case ToolId::Scale: return QStringLiteral("S");
    case ToolId::Wall: return QStringLiteral("W");
    case ToolId::Opening: return QStringLiteral("O");
    case ToolId::Room: return QStringLiteral("R");
    case ToolId::Stair: return QStringLiteral("T");
    case ToolId::Light: return QStringLiteral("L");
    case ToolId::Furniture: return QStringLiteral("P");
    case ToolId::Detect: return QStringLiteral("D");
    }
    return QString();
}

// Checks the active button, disables tools that cannot be used and updates tooltips.
void ToolStrip::refresh()
{
    for (ToolId id : kStripTools) {
        ToolButton* button = buttons_[static_cast<size_t>(id)];
        const bool available = controller_->isAvailable(id);
        button->setEnabled(available);
        button->setChecked(controller_->active() == id);
        QString tip = tr("%1 tool (%2)").arg(toolName(id), toolKey(id));
        if (!available)
            tip += QStringLiteral(". ") + controller_->unavailableReason(id);
        button->setToolTip(tip);
    }
}

// Paints the Panel background and the Line border on the right.
void ToolStrip::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.fillRect(rect(), t.panel());
    p.fillRect(QRect(width() - kBorderWidth, 0, kBorderWidth, height()), t.line());
}

}
