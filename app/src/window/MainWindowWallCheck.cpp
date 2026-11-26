#include "MainWindow.h"

#include "canvas/PlanCanvas.h"
#include "document/Selection.h"
#include "tools/ToolController.h"

#include <plynth/core/Rooms.h>
#include <plynth/core/WallGeometry.h>

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QMenu>
#include <QMouseEvent>
#include <QTextStream>
#include <QTimer>

#include <cmath>
#include <memory>
#include <vector>

namespace plynth::app {

namespace {

// Reads "a,b,c" as numbers. Empty when any part is not a number or the count differs.
std::vector<double> numbersOf(const QString& text, int count)
{
    std::vector<double> out;
    const QStringList parts = text.split(QLatin1Char(','));
    if (parts.size() != count)
        return {};
    for (const QString& part : parts) {
        bool ok = false;
        out.push_back(part.toDouble(&ok));
        if (!ok)
            return {};
    }
    return out;
}

// Describes a wall: its id, its two end positions and its thickness.
QString wallText(const plynth::Floor& floor, const plynth::Wall& wall)
{
    const plynth::Point* a = plynth::findPoint(floor, wall.from);
    const plynth::Point* b = plynth::findPoint(floor, wall.to);
    if (a == nullptr || b == nullptr)
        return QString::fromStdString(wall.id);
    return QStringLiteral("%1 (%2, %3) to (%4, %5) thickness %6")
        .arg(QString::fromStdString(wall.id))
        .arg(a->x, 0, 'g', 12)
        .arg(a->y, 0, 'g', 12)
        .arg(b->x, 0, 'g', 12)
        .arg(b->y, 0, 'g', 12)
        .arg(wall.thickness);
}

// Lists the rooms of a floor with their areas.
QString roomsText(const plynth::Floor& floor)
{
    QStringList rooms;
    for (const plynth::Room& room : floor.rooms)
        rooms << QStringLiteral("%1 %2 m2").arg(QString::fromStdString(room.id)).arg(plynth::roomShape(floor, room).area, 0, 'f', 2);
    return QStringLiteral("%1 rooms: %2").arg(floor.rooms.size()).arg(rooms.join(QStringLiteral(", ")));
}

}

// Sends one mouse event to the plan at a place in metres, as the mouse would.
void MainWindow::sendPlanMouse(QEvent::Type type, double x, double y, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    const double ppm = doc_.project().plan.scale.pixelsPerMetre.value_or(1.0);
    const QPointF local = canvas_->sceneToView(QPointF(x * ppm, y * ppm));
    QWidget* viewport = canvas_->viewport();
    const QPointF global = viewport->mapToGlobal(local);
    QMouseEvent event(type, local, global, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(viewport, &event);
    QApplication::processEvents();
}

// For checks only: drives the plan with mouse events as a user would and writes what happened to report.
// guides:x1,y1,x2,y2 draws a wall with the Wall tool and saves the plan with its guide lines to middlePicture just
// before the second click. join:point,x,y drags a point to a place, saves middlePicture after the drop, then runs
// Edit > Undo (Ctrl+Z). split:x,y right clicks a wall and picks Split here.
bool MainWindow::runWallCheck(const QString& spec, const QString& report, const QString& middlePicture)
{
    QString text;
    QTextStream out(&text);
    const int colon = spec.indexOf(QLatin1Char(':'));
    const QString kind = spec.left(colon);
    const QString args = spec.mid(colon + 1);
    const plynth::Floor* floor = tools_->floor();
    bool ok = colon > 0 && floor != nullptr && doc_.project().plan.scale.pixelsPerMetre.has_value();
    const auto current = [this]() { return *tools_->floor(); };

    if (ok && kind == QStringLiteral("guides")) {
        const std::vector<double> v = numbersOf(args, 4);
        ok = !v.empty() && activateToolByName(QStringLiteral("wall"));
        if (ok) {
            const std::size_t walls = current().walls.size();
            sendPlanMouse(QEvent::MouseMove, v[0], v[1], Qt::NoButton, Qt::NoButton);
            sendPlanMouse(QEvent::MouseButtonPress, v[0], v[1], Qt::LeftButton, Qt::LeftButton);
            sendPlanMouse(QEvent::MouseButtonRelease, v[0], v[1], Qt::LeftButton, Qt::NoButton);
            sendPlanMouse(QEvent::MouseMove, v[2], v[3], Qt::NoButton, Qt::NoButton);
            canvas_->viewport()->repaint();
            grab().save(middlePicture);
            sendPlanMouse(QEvent::MouseButtonPress, v[2], v[3], Qt::LeftButton, Qt::LeftButton);
            sendPlanMouse(QEvent::MouseButtonRelease, v[2], v[3], Qt::LeftButton, Qt::NoButton);
            const plynth::Floor after = current();
            ok = after.walls.size() == walls + 1;
            out << "cursor at the second click: " << v[2] << ", " << v[3] << "\n";
            if (ok)
                out << "new wall: " << wallText(after, after.walls.back()) << "\n";
            sendPlanMouse(QEvent::MouseMove, v[2] + 1.0, v[3] + 1.0, Qt::NoButton, Qt::NoButton);
        }
    } else if (ok && kind == QStringLiteral("join")) {
        const QStringList parts = args.split(QLatin1Char(','));
        const std::vector<double> v = parts.size() == 3 ? numbersOf(parts[1] + QLatin1Char(',') + parts[2], 2) : std::vector<double>{};
        const plynth::Point* point = v.empty() ? nullptr : plynth::findPoint(*floor, parts[0].toStdString());
        ok = point != nullptr && activateToolByName(QStringLiteral("select"));
        if (ok) {
            const plynth::Plan before = doc_.project().plan;
            const double x0 = point->x;
            const double y0 = point->y;
            out << "before: " << roomsText(current()) << "\n";
            sendPlanMouse(QEvent::MouseButtonPress, x0, y0, Qt::LeftButton, Qt::LeftButton);
            const int steps = 12;
            for (int i = 1; i <= steps; ++i) {
                const double t = static_cast<double>(i) / steps;
                sendPlanMouse(QEvent::MouseMove, x0 + (v[0] - x0) * t, y0 + (v[1] - y0) * t, Qt::NoButton, Qt::LeftButton);
            }
            canvas_->viewport()->repaint();
            sendPlanMouse(QEvent::MouseButtonRelease, v[0], v[1], Qt::LeftButton, Qt::NoButton);
            const plynth::Floor after = current();
            out << "point " << parts[0] << " still there: " << (plynth::findPoint(after, parts[0].toStdString()) ? "yes" : "no") << "\n";
            for (const plynth::Wall& wall : after.walls) {
                const plynth::Wall* old = nullptr;
                for (const plynth::Floor& f : before.floors)
                    if (const plynth::Wall* w = plynth::findWall(f, wall.id))
                        old = w;
                if (old == nullptr || old->from != wall.from || old->to != wall.to || old->thickness != wall.thickness)
                    out << "changed or new wall: " << wallText(after, wall) << "\n";
            }
            out << "after: " << roomsText(after) << "\n";
            out << "undo names: " << QString::fromStdString(doc_.undoName()) << "\n";
            canvas_->viewport()->repaint();
            grab().save(middlePicture);
            QAction* undo = nullptr;
            for (QAction* action : findChildren<QAction*>())
                if (action->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_Z)))
                    undo = action;
            ok = undo != nullptr && undo->isEnabled();
            if (ok)
                undo->trigger();
            QApplication::processEvents();
            const bool restored = doc_.project().plan == before;
            out << "after one Ctrl+Z (Edit > Undo): plan as before the drag: " << (restored ? "yes" : "no")
                << ", more to undo: " << (doc_.canUndo() ? "yes" : "no") << "\n";
            ok = ok && restored;
        }
    } else if (ok && kind == QStringLiteral("split")) {
        const std::vector<double> v = numbersOf(args, 2);
        ok = !v.empty() && activateToolByName(QStringLiteral("select"));
        if (ok) {
            const std::size_t points = current().points.size();
            auto picked = std::make_shared<bool>(false);
            QTimer::singleShot(300, this, [picked]() {
                auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                if (menu == nullptr)
                    return;
                for (QAction* action : menu->actions()) {
                    if (action->text() == QObject::tr("Split here")) {
                        *picked = true;
                        action->trigger();
                    }
                }
                menu->close();
            });
            sendPlanMouse(QEvent::MouseMove, v[0], v[1], Qt::NoButton, Qt::NoButton);
            sendPlanMouse(QEvent::MouseButtonPress, v[0], v[1], Qt::RightButton, Qt::RightButton);
            sendPlanMouse(QEvent::MouseButtonRelease, v[0], v[1], Qt::RightButton, Qt::NoButton);
            const plynth::Floor after = current();
            out << "menu shown with Split here: " << (*picked ? "yes" : "no") << "\n";
            out << "points before " << points << ", after " << after.points.size() << "\n";
            out << "selection: " << (selection_->kind() == SelectionKind::Point ? "point " : "not a point ") << selection_->id() << "\n";
            if (const plynth::Point* p = plynth::findPoint(after, selection_->id().toStdString()))
                out << "selected point at " << p->x << ", " << p->y << "\n";
            ok = *picked && after.points.size() == points + 1 && selection_->kind() == SelectionKind::Point;
        }
    } else {
        ok = false;
    }
    out << "result: " << (ok ? "ok" : "failed") << "\n";
    out.flush();
    QFile file(report);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(text.toUtf8());
    return ok;
}

}
