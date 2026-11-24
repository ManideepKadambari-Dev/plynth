#pragma once

#include "tools/Tool.h"

#include <QAbstractButton>
#include <QWidget>

#include <array>

namespace plynth::app {

class ToolController;

// Every tool in the order of the tool strip and the Edit menu, the Stair and Light tools after Room.
inline constexpr std::array<ToolId, kToolCount> kStripTools{ToolId::Select,  ToolId::Scale, ToolId::Wall,
                                                            ToolId::Opening, ToolId::Room,  ToolId::Stair,
                                                            ToolId::Light,   ToolId::Furniture, ToolId::Detect};

// One square tool button with an icon drawn in code.
class ToolButton : public QAbstractButton {
    Q_OBJECT
public:
    ToolButton(ToolId tool, QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    ToolId tool_;
    bool hovered_ = false;
};

// The column of tool buttons at the left edge of the canvas area.
class ToolStrip : public QWidget {
    Q_OBJECT
public:
    explicit ToolStrip(ToolController* controller, QWidget* parent = nullptr);

    // Reads the active tool and which tools can be used from the controller.
    void refresh();
    // The name and shortcut shown in tooltips and menus.
    static QString toolName(ToolId id);
    static QString toolKey(ToolId id);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    ToolController* controller_;
    std::array<ToolButton*, kToolCount> buttons_{};
};

}
