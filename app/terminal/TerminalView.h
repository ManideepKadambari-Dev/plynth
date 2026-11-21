#pragma once

#include <QColor>
#include <QFont>
#include <QPoint>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <array>

#include "TerminalScreen.h"

namespace plynth::terminal {

class TerminalSession;

// Everything the view needs to look right. The caller fills it from the theme.
struct TerminalPalette {
    QColor background;
    QColor foreground;
    QColor cursor;
    QColor selection;  // drawn over the text, so it should carry an alpha
    QColor scrollIndicator;
    std::array<QColor, 16> ansi;
    QString fontFamily;
    int pixelSize = 14;
    int padding = 6;
};

// Draws a terminal screen and turns keys and mouse input into bytes for the program.
class TerminalView : public QWidget {
    Q_OBJECT
public:
    TerminalView(TerminalSession* session, const TerminalPalette& palette, QWidget* parent = nullptr);

    void setPalette(const TerminalPalette& palette);
    QSize cellSize() const { return cell_; }
    // The widget size that holds exactly this many cells.
    QSize sizeForCells(int columns, int rows) const;
    QSize sizeHint() const override { return sizeForCells(80, 24); }

    bool hasSelection() const;
    QString selectedText() const;
    void copySelection();
    void paste();
    void scrollBackBy(int lines);
    int scrollOffset() const { return offset_; }

    // Colors as they are drawn, for tests.
    QColor resolveColor(const TerminalColor& color, bool foreground) const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;
    bool focusNextPrevChild(bool next) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Cell {
        int line;  // row counted from the oldest scrollback line
        int column;
    };

    void updateMetrics();
    void markDamaged(int firstRow, int lastRow);
    void flushDamage();
    Cell cellAt(const QPoint& pos) const;
    int displayRowToScreenRow(int displayRow) const;
    int totalLines() const;
    QRect rowRect(int displayRow) const;
    bool selected(int line, int column) const;
    void clearSelection();
    bool mouseReported(const QMouseEvent* event) const;
    void sendMouse(const QMouseEvent* event, bool pressed, bool move);

    TerminalSession* session_;
    TerminalPalette palette_;
    QFont font_;
    QSize cell_{8, 16};
    int baseline_ = 12;
    int offset_ = 0;  // how many lines the view is scrolled back

    QTimer damageTimer_;
    int dirtyFirst_ = -1;
    int dirtyLast_ = -1;

    bool selecting_ = false;
    bool hasSelection_ = false;
    Cell selStart_{0, 0};
    Cell selEnd_{0, 0};
    int buttonsDown_ = 0;
};

}  // namespace plynth::terminal
