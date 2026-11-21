#include "TerminalView.h"

#include <QApplication>
#include <QClipboard>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QStringList>
#include <QWheelEvent>

#include <algorithm>
#include <vector>

#include "TerminalSession.h"

namespace plynth::terminal {

namespace {

constexpr int kDamageIntervalMs = 16;

// Maps a Qt key to a terminal key. Returns false when the key is not a special key.
bool mapKey(int key, TerminalKey* out) {
    switch (key) {
        case Qt::Key_Return: case Qt::Key_Enter: *out = TerminalKey::Enter; return true;
        case Qt::Key_Tab: *out = TerminalKey::Tab; return true;
        case Qt::Key_Backtab: *out = TerminalKey::Tab; return true;
        case Qt::Key_Backspace: *out = TerminalKey::Backspace; return true;
        case Qt::Key_Escape: *out = TerminalKey::Escape; return true;
        case Qt::Key_Up: *out = TerminalKey::Up; return true;
        case Qt::Key_Down: *out = TerminalKey::Down; return true;
        case Qt::Key_Left: *out = TerminalKey::Left; return true;
        case Qt::Key_Right: *out = TerminalKey::Right; return true;
        case Qt::Key_Insert: *out = TerminalKey::Insert; return true;
        case Qt::Key_Delete: *out = TerminalKey::Delete; return true;
        case Qt::Key_Home: *out = TerminalKey::Home; return true;
        case Qt::Key_End: *out = TerminalKey::End; return true;
        case Qt::Key_PageUp: *out = TerminalKey::PageUp; return true;
        case Qt::Key_PageDown: *out = TerminalKey::PageDown; return true;
        default: break;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        *out = static_cast<TerminalKey>(static_cast<int>(TerminalKey::F1) + (key - Qt::Key_F1));
        return true;
    }
    return false;
}

}  // namespace

TerminalView::TerminalView(TerminalSession* session, const TerminalPalette& palette, QWidget* parent)
    : QWidget(parent), session_(session), palette_(palette) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::IBeamCursor);
    setToolTip(tr("Terminal"));
    updateMetrics();

    damageTimer_.setSingleShot(true);
    damageTimer_.setInterval(kDamageIntervalMs);
    connect(&damageTimer_, &QTimer::timeout, this, &TerminalView::flushDamage);
    connect(session_, &TerminalSession::screenDamaged, this, &TerminalView::markDamaged);
}

// Applies new colors and font, then redraws.
void TerminalView::setPalette(const TerminalPalette& palette) {
    palette_ = palette;
    updateMetrics();
    updateGeometry();
    update();
}

// Measures one character cell from the font.
void TerminalView::updateMetrics() {
    font_ = QFont(palette_.fontFamily);
    font_.setStyleHint(QFont::Monospace);
    font_.setPixelSize(palette_.pixelSize);
    font_.setKerning(false);
    QFontMetrics fm(font_);
    cell_ = QSize(std::max(1, fm.horizontalAdvance(QLatin1Char('M'))), std::max(1, fm.height()));
    baseline_ = fm.ascent();
}

QSize TerminalView::sizeForCells(int columns, int rows) const {
    return QSize(columns * cell_.width() + 2 * palette_.padding, rows * cell_.height() + 2 * palette_.padding);
}

// Remembers which rows changed and redraws them after a short delay, so bursts of output draw once.
void TerminalView::markDamaged(int firstRow, int lastRow) {
    if (dirtyFirst_ < 0) {
        dirtyFirst_ = firstRow;
        dirtyLast_ = lastRow;
    } else {
        dirtyFirst_ = std::min(dirtyFirst_, firstRow);
        dirtyLast_ = std::max(dirtyLast_, lastRow);
    }
    if (!damageTimer_.isActive()) damageTimer_.start();
}

void TerminalView::flushDamage() {
    int first = dirtyFirst_;
    int last = dirtyLast_;
    dirtyFirst_ = dirtyLast_ = -1;
    if (first < 0) return;
    const TerminalScreen& screen = session_->screen();
    if (offset_ > 0 && !screen.alternateScreen()) {
        update();
        return;
    }
    update(rowRect(first).united(rowRect(last)));
}

int TerminalView::totalLines() const {
    return session_->screen().scrollbackLines() + session_->screen().rows();
}

int TerminalView::displayRowToScreenRow(int displayRow) const {
    int offset = session_->screen().alternateScreen() ? 0 : offset_;
    return displayRow - offset;
}

QRect TerminalView::rowRect(int displayRow) const {
    return QRect(0, palette_.padding + displayRow * cell_.height(), width(), cell_.height());
}

// Turns a terminal color into a drawing color.
QColor TerminalView::resolveColor(const TerminalColor& color, bool foreground) const {
    switch (color.kind) {
        case TerminalColor::Kind::Default:
            return foreground ? palette_.foreground : palette_.background;
        case TerminalColor::Kind::Rgb:
            return QColor(color.r, color.g, color.b);
        case TerminalColor::Kind::Indexed:
            break;
    }
    int i = color.index;
    if (i < 16) return palette_.ansi[static_cast<std::size_t>(i)];
    if (i < 232) {
        int n = i - 16;
        auto level = [](int v) { return v == 0 ? 0 : 55 + v * 40; };
        return QColor(level(n / 36), level((n / 6) % 6), level(n % 6));
    }
    int grey = 8 + (i - 232) * 10;
    return QColor(grey, grey, grey);
}

bool TerminalView::selected(int line, int column) const {
    if (!hasSelection_) return false;
    Cell a = selStart_;
    Cell b = selEnd_;
    if (a.line > b.line || (a.line == b.line && a.column > b.column)) std::swap(a, b);
    if (line < a.line || line > b.line) return false;
    if (line == a.line && column < a.column) return false;
    if (line == b.line && column > b.column) return false;
    return true;
}

// Draws the visible rows: background runs first, then the characters, then selection and cursor.
void TerminalView::paintEvent(QPaintEvent* event) {
    QPainter p(this);
    p.fillRect(event->rect(), palette_.background);
    const TerminalScreen& screen = session_->screen();
    const int cols = screen.columns();
    const int rows = screen.rows();
    const int scrollback = screen.scrollbackLines();
    const int offset = screen.alternateScreen() ? 0 : offset_;
    const int pad = palette_.padding;
    const int cw = cell_.width();
    const int ch = cell_.height();

    int firstRow = std::max(0, (event->rect().top() - pad) / ch);
    int lastRow = std::min(rows - 1, (event->rect().bottom() - pad) / ch);
    p.setFont(font_);

    for (int d = firstRow; d <= lastRow; ++d) {
        int screenRow = d - offset;
        int y = pad + d * ch;

        std::vector<TerminalCell> cells(static_cast<std::size_t>(cols));
        for (int c = 0; c < cols; ++c) cells[static_cast<std::size_t>(c)] = screen.cell(screenRow, c);

        auto colorsOf = [&](const TerminalCell& cl, QColor* fg, QColor* bg) {
            *fg = resolveColor(cl.fg, true);
            *bg = resolveColor(cl.bg, false);
            if (cl.reverse) std::swap(*fg, *bg);
        };

        int c = 0;
        while (c < cols) {
            QColor fg, bg;
            colorsOf(cells[static_cast<std::size_t>(c)], &fg, &bg);
            int end = c + 1;
            while (end < cols) {
                QColor fg2, bg2;
                colorsOf(cells[static_cast<std::size_t>(end)], &fg2, &bg2);
                if (bg2 != bg) break;
                ++end;
            }
            if (bg != palette_.background) p.fillRect(pad + c * cw, y, (end - c) * cw, ch, bg);
            c = end;
        }

        for (c = 0; c < cols; ++c) {
            const TerminalCell& cl = cells[static_cast<std::size_t>(c)];
            if (cl.width == 0) continue;
            QColor fg, bg;
            colorsOf(cl, &fg, &bg);
            int x = pad + c * cw;
            if (!cl.text.empty() && cl.text != " ") {
                if (cl.bold != font_.bold() || cl.italic != font_.italic()) {
                    QFont f = font_;
                    f.setBold(cl.bold);
                    f.setItalic(cl.italic);
                    p.setFont(f);
                }
                p.setPen(fg);
                p.drawText(x, y + baseline_, QString::fromUtf8(cl.text.data(), static_cast<qsizetype>(cl.text.size())));
                if (cl.bold != font_.bold() || cl.italic != font_.italic()) p.setFont(font_);
            }
            if (cl.underline) p.fillRect(x, y + baseline_ + 1, cw * std::max(1, cl.width), 1, fg);
        }

        if (hasSelection_) {
            int line = scrollback + screenRow;
            for (c = 0; c < cols; ++c) {
                if (selected(line, c)) p.fillRect(pad + c * cw, y, cw, ch, palette_.selection);
            }
        }
    }

    if (offset == 0 && screen.cursorVisible()) {
        int row = screen.cursorRow();
        int col = screen.cursorColumn();
        if (row >= firstRow && row <= lastRow && col < cols) {
            TerminalCell under = screen.cell(row, col);
            int w = cw * std::max(1, under.width);
            QRect r(pad + col * cw, pad + row * ch, w, ch);
            if (hasFocus()) {
                p.fillRect(r, palette_.cursor);
                if (!under.text.empty() && under.text != " ") {
                    p.setPen(palette_.background);
                    p.drawText(r.left(), r.top() + baseline_,
                               QString::fromUtf8(under.text.data(), static_cast<qsizetype>(under.text.size())));
                }
            } else {
                p.fillRect(QRect(r.left(), r.top(), r.width(), 1), palette_.cursor);
                p.fillRect(QRect(r.left(), r.bottom(), r.width(), 1), palette_.cursor);
                p.fillRect(QRect(r.left(), r.top(), 1, r.height()), palette_.cursor);
                p.fillRect(QRect(r.right(), r.top(), 1, r.height()), palette_.cursor);
            }
        }
    }

    if (offset > 0 && scrollback > 0) {
        int trackH = height() - 2 * pad;
        int barH = std::max(12, trackH * rows / totalLines());
        int barY = pad + (trackH - barH) * (scrollback - offset) / scrollback;
        p.fillRect(width() - 4, barY, 3, barH, palette_.scrollIndicator);
    }
}

// Keeps the grid size in step with the widget size and tells the program.
void TerminalView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    int cols = std::max(2, (width() - 2 * palette_.padding) / cell_.width());
    int rows = std::max(1, (height() - 2 * palette_.padding) / cell_.height());
    if (cols != session_->screen().columns() || rows != session_->screen().rows()) session_->resize(cols, rows);
    offset_ = std::min(offset_, session_->screen().scrollbackLines());
}

// Lets the terminal take every key, so window shortcuts such as Ctrl+S do not fire. Ctrl+` stays a shortcut.
bool TerminalView::event(QEvent* event) {
    if (event->type() == QEvent::ShortcutOverride) {
        auto* ke = static_cast<QKeyEvent*>(event);
        bool toggle = ke->key() == Qt::Key_QuoteLeft && (ke->modifiers() & Qt::ControlModifier);
        if (toggle) return QWidget::event(event);
        event->accept();
        return true;
    }
    return QWidget::event(event);
}

bool TerminalView::focusNextPrevChild(bool) {
    return false;
}

void TerminalView::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    update();
}

void TerminalView::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
    update();
}

// Sends a key press to the program, with the terminal's own rules for copy, paste and scrolling.
void TerminalView::keyPressEvent(QKeyEvent* event) {
    const Qt::KeyboardModifiers m = event->modifiers();
    const int key = event->key();
    const bool ctrl = m & Qt::ControlModifier;
    const bool shift = m & Qt::ShiftModifier;
    const bool alt = m & Qt::AltModifier;
    TerminalScreen& screen = session_->screen();

    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta ||
        key == Qt::Key_AltGr || key == Qt::Key_CapsLock) {
        return;
    }

    if (ctrl && shift && key == Qt::Key_C) {
        copySelection();
        return;
    }
    if (ctrl && key == Qt::Key_C && hasSelection_) {
        copySelection();
        clearSelection();
        return;
    }
    if (ctrl && key == Qt::Key_V) {
        paste();
        return;
    }
    if (shift && !ctrl && !alt && (key == Qt::Key_PageUp || key == Qt::Key_PageDown)) {
        int page = std::max(1, screen.rows() - 1);
        scrollBackBy(key == Qt::Key_PageUp ? page : -page);
        return;
    }

    offset_ = 0;
    clearSelection();
    update();

    KeyMods mods{shift, alt, ctrl};
    TerminalKey special;
    if (mapKey(key, &special)) {
        if (key == Qt::Key_Backtab) mods.shift = true;
        screen.sendKey(special, mods);
        return;
    }

    const QString text = event->text();
    // AltGr arrives as Ctrl+Alt on Windows; the typed character is already in the text.
    if (ctrl && alt && !text.isEmpty() && text.at(0).unicode() >= 0x20) {
        screen.sendText(text.toStdU32String(), KeyMods{});
        return;
    }
    if (ctrl && key >= Qt::Key_A && key <= Qt::Key_Z) {
        screen.sendText(std::u32string(1, static_cast<char32_t>(U'a' + (key - Qt::Key_A))), KeyMods{shift, alt, true});
        return;
    }
    if (ctrl && key == Qt::Key_Space) {
        screen.sendText(U" ", KeyMods{false, alt, true});
        return;
    }
    if (ctrl && key >= 0x20 && key < 0x7f) {
        screen.sendText(std::u32string(1, static_cast<char32_t>(key)), KeyMods{false, alt, true});
        return;
    }
    if (!text.isEmpty() && text.at(0).unicode() >= 0x20) {
        screen.sendText(text.toStdU32String(), KeyMods{false, alt, false});
        return;
    }
    if (!text.isEmpty()) {
        screen.sendText(text.toStdU32String(), KeyMods{false, alt, false});
    }
}

bool TerminalView::hasSelection() const {
    return hasSelection_;
}

// Gives the selected text, one line per row, with trailing blanks removed.
QString TerminalView::selectedText() const {
    if (!hasSelection_) return {};
    const TerminalScreen& screen = session_->screen();
    Cell a = selStart_;
    Cell b = selEnd_;
    if (a.line > b.line || (a.line == b.line && a.column > b.column)) std::swap(a, b);
    const int scrollback = screen.scrollbackLines();
    QStringList lines;
    for (int line = a.line; line <= b.line; ++line) {
        int row = line - scrollback;
        int from = line == a.line ? a.column : 0;
        int to = line == b.line ? b.column : screen.columns() - 1;
        std::string text;
        for (int c = from; c <= to && c < screen.columns(); ++c) {
            TerminalCell cl = screen.cell(row, c);
            if (cl.width == 0) continue;
            text += cl.text.empty() ? std::string(" ") : cl.text;
        }
        while (!text.empty() && text.back() == ' ') text.pop_back();
        lines << QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    }
    return lines.join(QLatin1Char('\n'));
}

void TerminalView::copySelection() {
    if (hasSelection_) QApplication::clipboard()->setText(selectedText());
}

// Pastes the clipboard text. Line breaks become carriage returns, as typing Enter would.
void TerminalView::paste() {
    QString text = QApplication::clipboard()->text();
    if (text.isEmpty()) return;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\r"));
    text.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    offset_ = 0;
    session_->screen().sendPaste(text.toUtf8().toStdString());
    update();
}

// Scrolls through the history. Positive moves back in time.
void TerminalView::scrollBackBy(int lines) {
    if (session_->screen().alternateScreen()) return;
    int next = std::clamp(offset_ + lines, 0, session_->screen().scrollbackLines());
    if (next == offset_) return;
    offset_ = next;
    update();
}

void TerminalView::clearSelection() {
    if (!hasSelection_) return;
    hasSelection_ = false;
    update();
}

TerminalView::Cell TerminalView::cellAt(const QPoint& pos) const {
    const TerminalScreen& screen = session_->screen();
    int col = std::clamp((pos.x() - palette_.padding) / cell_.width(), 0, screen.columns() - 1);
    int row = std::clamp((pos.y() - palette_.padding) / cell_.height(), 0, screen.rows() - 1);
    return Cell{screen.scrollbackLines() + displayRowToScreenRow(row), col};
}

// A program that asked for mouse reports gets them, unless Shift is held to select instead.
bool TerminalView::mouseReported(const QMouseEvent* event) const {
    return session_->screen().mouseMode() != MouseMode::None && !(event->modifiers() & Qt::ShiftModifier);
}

void TerminalView::sendMouse(const QMouseEvent* event, bool pressed, bool move) {
    TerminalScreen& screen = session_->screen();
    int col = std::clamp((event->pos().x() - palette_.padding) / cell_.width(), 0, screen.columns() - 1);
    int row = std::clamp((event->pos().y() - palette_.padding) / cell_.height(), 0, screen.rows() - 1);
    KeyMods mods{bool(event->modifiers() & Qt::ShiftModifier), bool(event->modifiers() & Qt::AltModifier),
                 bool(event->modifiers() & Qt::ControlModifier)};
    screen.sendMouseMove(row, col, mods);
    if (move) return;
    int button = event->button() == Qt::LeftButton ? 1 : event->button() == Qt::MiddleButton ? 2 : 3;
    screen.sendMouseButton(button, pressed, mods);
}

void TerminalView::mousePressEvent(QMouseEvent* event) {
    setFocus();
    if (mouseReported(event)) {
        buttonsDown_ |= event->button();
        sendMouse(event, true, false);
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    selStart_ = selEnd_ = cellAt(event->pos());
    selecting_ = true;
    hasSelection_ = false;
    update();
}

void TerminalView::mouseMoveEvent(QMouseEvent* event) {
    if (mouseReported(event)) {
        MouseMode mode = session_->screen().mouseMode();
        if (mode == MouseMode::Move || (mode == MouseMode::Drag && buttonsDown_)) sendMouse(event, false, true);
        return;
    }
    if (!selecting_) return;
    selEnd_ = cellAt(event->pos());
    hasSelection_ = selStart_.line != selEnd_.line || selStart_.column != selEnd_.column;
    update();
}

void TerminalView::mouseReleaseEvent(QMouseEvent* event) {
    if (buttonsDown_ & event->button()) {
        buttonsDown_ &= ~static_cast<int>(event->button());
        sendMouse(event, false, false);
        return;
    }
    selecting_ = false;
}

// Selects the word under the pointer.
void TerminalView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (mouseReported(event) || event->button() != Qt::LeftButton) return;
    const TerminalScreen& screen = session_->screen();
    Cell at = cellAt(event->pos());
    int row = at.line - screen.scrollbackLines();
    auto isWord = [&](int c) {
        TerminalCell cl = screen.cell(row, c);
        return !cl.text.empty() && cl.text != " ";
    };
    if (!isWord(at.column)) return;
    int from = at.column;
    int to = at.column;
    while (from > 0 && isWord(from - 1)) --from;
    while (to < screen.columns() - 1 && isWord(to + 1)) ++to;
    selStart_ = Cell{at.line, from};
    selEnd_ = Cell{at.line, to};
    hasSelection_ = true;
    selecting_ = false;
    update();
}

// Scrolls the history, or passes the wheel to a program that asked for the mouse.
void TerminalView::wheelEvent(QWheelEvent* event) {
    int notches = event->angleDelta().y() / 120;
    if (notches == 0) return;
    TerminalScreen& screen = session_->screen();
    if (screen.mouseMode() != MouseMode::None && !(event->modifiers() & Qt::ShiftModifier)) {
        int col = std::clamp((static_cast<int>(event->position().x()) - palette_.padding) / cell_.width(), 0, screen.columns() - 1);
        int row = std::clamp((static_cast<int>(event->position().y()) - palette_.padding) / cell_.height(), 0, screen.rows() - 1);
        KeyMods mods{bool(event->modifiers() & Qt::ShiftModifier), bool(event->modifiers() & Qt::AltModifier),
                     bool(event->modifiers() & Qt::ControlModifier)};
        screen.sendMouseMove(row, col, mods);
        for (int i = 0; i < std::abs(notches); ++i) screen.sendMouseButton(notches > 0 ? 4 : 5, true, mods);
        return;
    }
    scrollBackBy(notches * 3);
}

}  // namespace plynth::terminal
