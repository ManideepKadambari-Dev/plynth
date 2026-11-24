#include "guide/TourAudit.h"

#include "guide/GuideHost.h"
#include "window/MainWindow.h"

#include <plynth/core/Tour.h>

#include <nlohmann/json.hpp>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QTabBar>

#include <set>
#include <string>
#include <vector>

namespace plynth::app {

namespace {

// Says whether a widget is a control a person uses: a button, a text box, a choice, a number box, a slider,
// a list or a tab bar. Scroll bars belong to the view they scroll.
bool isControl(const QWidget* widget)
{
    if (qobject_cast<const QScrollBar*>(widget)) return false;
    return qobject_cast<const QAbstractButton*>(widget) || qobject_cast<const QLineEdit*>(widget) ||
           qobject_cast<const QComboBox*>(widget) || qobject_cast<const QAbstractSpinBox*>(widget) ||
           qobject_cast<const QAbstractSlider*>(widget) || qobject_cast<const QAbstractItemView*>(widget) ||
           qobject_cast<const QTabBar*>(widget);
}

// Says whether a control sits inside another control, such as the text box of a number box. It counts with
// its parent.
bool insideControl(const QWidget* widget, const QWidget* top)
{
    for (const QWidget* up = widget->parentWidget(); up && up != top; up = up->parentWidget()) {
        if (isControl(up)) return true;
    }
    return false;
}

// The tour id of a widget, or of the nearest parent that has one.
QString idOf(const QWidget* widget, const QWidget* top)
{
    for (const QWidget* w = widget; w; w = w->parentWidget()) {
        const QString id = w->property("tourId").toString();
        if (!id.isEmpty()) return id;
        if (w == top) break;
    }
    return {};
}

// Text from Qt as UTF-8 for core.
std::string toCore(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

}

// Visits each screen, gathers the ids and asks core whether every one has an entry.
int runTourAudit(MainWindow& window, const QString& outFile)
{
    GuideHost* guide = window.guide();
    std::set<std::string> ids;
    std::size_t withoutId = 0;
    std::vector<std::string> unnamed;
    const QString startScreen = guide->currentScreen();
    for (std::string_view screen : plynth::tourScreens()) {
        if (!guide->showScreen(QString::fromUtf8(screen.data(), static_cast<qsizetype>(screen.size())))) continue;
        for (int pass = 0; pass < 4; ++pass) QApplication::processEvents();
        for (QWidget* widget : window.findChildren<QWidget*>()) {
            if (!widget->isVisible() || !isControl(widget) || insideControl(widget, &window)) continue;
            const QString id = idOf(widget, &window);
            if (id.isEmpty()) {
                ++withoutId;
                unnamed.push_back(toCore(QString::fromLatin1(widget->metaObject()->className()) + QStringLiteral(" ") +
                                         widget->objectName()));
            } else {
                ids.insert(toCore(id));
            }
        }
    }
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->isSeparator() || action->text().isEmpty()) continue;
        bool inMenu = false;
        for (QObject* owner : action->associatedObjects()) inMenu = inMenu || qobject_cast<QMenu*>(owner) != nullptr;
        if (!inMenu) continue;
        const QString id = action->property("tourId").toString();
        if (id.isEmpty() && action->menu()) continue;
        if (id.isEmpty()) {
            ++withoutId;
            unnamed.push_back(toCore(QStringLiteral("menu entry ") + action->text()));
        } else {
            ids.insert(toCore(id));
        }
    }
    guide->showScreen(startScreen);

    const std::vector<std::string> list(ids.begin(), ids.end());
    const plynth::TourCheck check = plynth::checkTourEntries(guide->tourData(), list, withoutId, {}, {});
    nlohmann::ordered_json json = nlohmann::ordered_json::object();
    json["format"] = "plynth-tour-audit";
    json["version"] = 1;
    json["tour_loaded"] = guide->tourLoaded();
    json["controls"] = list;
    json["without_entry"] = check.withoutEntry;
    json["without_id"] = check.withoutId;
    json["without_id_examples"] = unnamed.size() > 40 ? std::vector<std::string>(unnamed.begin(), unnamed.begin() + 40) : unnamed;
    json["unused_entries"] = check.unused;
    // Controls without any id and entries no visited screen showed are listed but do not fail the audit yet:
    // dialogs and states the audit does not open hold most of them.
    const bool passed = guide->tourLoaded() && check.withoutEntry.empty();
    json["ok"] = passed;
    QFile file(outFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 1;
    file.write(QByteArray::fromStdString(json.dump(2) + "\n"));
    file.close();
    return passed ? 0 : kExitTourAuditFailed;
}

}
