#include "TerminalScreen.h"

#include <vterm.h>

#include <algorithm>
#include <deque>
#include <vector>

namespace plynth::terminal {

namespace {

// Appends one code point to a UTF-8 string.
void appendUtf8(std::string& out, std::uint32_t c) {
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (c >> 18)));
        out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

// Turns a libvterm color into the plain color type.
TerminalColor convertColor(const VTermColor& color) {
    TerminalColor out;
    if (VTERM_COLOR_IS_DEFAULT_FG(&color) || VTERM_COLOR_IS_DEFAULT_BG(&color)) return out;
    if (VTERM_COLOR_IS_INDEXED(&color)) {
        out.kind = TerminalColor::Kind::Indexed;
        out.index = color.indexed.idx;
    } else {
        out.kind = TerminalColor::Kind::Rgb;
        out.r = color.rgb.red;
        out.g = color.rgb.green;
        out.b = color.rgb.blue;
    }
    return out;
}

VTermModifier convertMods(KeyMods mods) {
    int m = VTERM_MOD_NONE;
    if (mods.shift) m |= VTERM_MOD_SHIFT;
    if (mods.alt) m |= VTERM_MOD_ALT;
    if (mods.ctrl) m |= VTERM_MOD_CTRL;
    return static_cast<VTermModifier>(m);
}

}  // namespace

struct TerminalScreen::Impl {
    VTerm* vt = nullptr;
    VTermScreen* screen = nullptr;
    std::deque<std::vector<VTermScreenCell>> scrollback;
    std::function<void(int, int)> onDamage;
    std::function<void(const char*, std::size_t)> onOutput;
    bool cursorVisible = true;
    bool altScreen = false;
    MouseMode mouse = MouseMode::None;
    bool probing = false;
    std::string probeBytes;
    int rows = 0;
    int columns = 0;

    // Gives a blank cell in the default colors.
    VTermScreenCell blankCell() const {
        VTermScreenCell cell{};
        cell.width = 1;
        vterm_state_get_default_colors(vterm_obtain_state(vt), &cell.fg, &cell.bg);
        return cell;
    }

    void damage(int first, int last) {
        if (onDamage) onDamage(std::max(first, 0), std::min(last, rows - 1));
    }

    static int onDamageRect(VTermRect rect, void* user) {
        static_cast<Impl*>(user)->damage(rect.start_row, rect.end_row - 1);
        return 1;
    }
    static int onMoveRect(VTermRect dest, VTermRect src, void* user) {
        auto* self = static_cast<Impl*>(user);
        self->damage(std::min(dest.start_row, src.start_row), std::max(dest.end_row, src.end_row) - 1);
        return 1;
    }
    static int onMoveCursor(VTermPos pos, VTermPos oldPos, int, void* user) {
        auto* self = static_cast<Impl*>(user);
        self->damage(std::min(pos.row, oldPos.row), std::max(pos.row, oldPos.row));
        return 1;
    }
    static int onSetProp(VTermProp prop, VTermValue* value, void* user) {
        auto* self = static_cast<Impl*>(user);
        switch (prop) {
            case VTERM_PROP_CURSORVISIBLE:
                self->cursorVisible = value->boolean != 0;
                self->damage(0, self->rows - 1);
                break;
            case VTERM_PROP_ALTSCREEN:
                self->altScreen = value->boolean != 0;
                self->damage(0, self->rows - 1);
                break;
            case VTERM_PROP_MOUSE:
                self->mouse = value->number == VTERM_PROP_MOUSE_CLICK  ? MouseMode::Click
                              : value->number == VTERM_PROP_MOUSE_DRAG ? MouseMode::Drag
                              : value->number == VTERM_PROP_MOUSE_MOVE ? MouseMode::Move
                                                                        : MouseMode::None;
                break;
            default:
                break;
        }
        return 1;
    }
    static int onPushLine(int cols, const VTermScreenCell* cells, void* user) {
        auto* self = static_cast<Impl*>(user);
        self->scrollback.emplace_back(cells, cells + cols);
        if (static_cast<int>(self->scrollback.size()) > kMaxScrollback) self->scrollback.pop_front();
        return 1;
    }
    static int onPopLine(int cols, VTermScreenCell* cells, void* user) {
        auto* self = static_cast<Impl*>(user);
        if (self->scrollback.empty()) return 0;
        const auto& line = self->scrollback.back();
        VTermScreenCell blank = self->blankCell();
        for (int i = 0; i < cols; ++i) cells[i] = i < static_cast<int>(line.size()) ? line[i] : blank;
        self->scrollback.pop_back();
        return 1;
    }
    static int onClearScrollback(void* user) {
        static_cast<Impl*>(user)->scrollback.clear();
        return 1;
    }
    static void onOutputBytes(const char* bytes, std::size_t size, void* user) {
        auto* self = static_cast<Impl*>(user);
        if (self->probing) {
            self->probeBytes.append(bytes, size);
        } else if (self->onOutput) {
            self->onOutput(bytes, size);
        }
    }
};

TerminalScreen::TerminalScreen(int rows, int columns) : impl_(std::make_unique<Impl>()) {
    impl_->rows = rows;
    impl_->columns = columns;
    impl_->vt = vterm_new(rows, columns);
    vterm_set_utf8(impl_->vt, 1);
    impl_->screen = vterm_obtain_screen(impl_->vt);

    static const VTermScreenCallbacks callbacks = {
        &Impl::onDamageRect, &Impl::onMoveRect, &Impl::onMoveCursor, &Impl::onSetProp, nullptr, nullptr,
        &Impl::onPushLine, &Impl::onPopLine, &Impl::onClearScrollback};
    vterm_screen_set_callbacks(impl_->screen, &callbacks, impl_.get());
    vterm_screen_enable_altscreen(impl_->screen, 1);
    vterm_screen_set_damage_merge(impl_->screen, VTERM_DAMAGE_SCROLL);
    vterm_output_set_callback(impl_->vt, &Impl::onOutputBytes, impl_.get());
    vterm_screen_reset(impl_->screen, 1);
}

TerminalScreen::~TerminalScreen() {
    vterm_free(impl_->vt);
}

// Gives bytes from the program to the emulator.
void TerminalScreen::feed(const char* bytes, std::size_t size) {
    vterm_input_write(impl_->vt, bytes, size);
    vterm_screen_flush_damage(impl_->screen);
}

// Changes the grid size, keeping the text.
void TerminalScreen::resize(int rows, int columns) {
    if (rows < 1 || columns < 1 || (rows == impl_->rows && columns == impl_->columns)) return;
    impl_->rows = rows;
    impl_->columns = columns;
    vterm_set_size(impl_->vt, rows, columns);
    vterm_screen_flush_damage(impl_->screen);
    impl_->damage(0, rows - 1);
}

int TerminalScreen::rows() const { return impl_->rows; }
int TerminalScreen::columns() const { return impl_->columns; }

// Reads one cell from the screen or, with a negative row, from the scrollback.
TerminalCell TerminalScreen::cell(int row, int column) const {
    TerminalCell out;
    if (column < 0 || column >= impl_->columns) return out;
    VTermScreenCell raw = impl_->blankCell();
    if (row < 0) {
        int index = static_cast<int>(impl_->scrollback.size()) + row;
        if (index < 0) return out;
        const auto& line = impl_->scrollback[static_cast<std::size_t>(index)];
        if (column < static_cast<int>(line.size())) raw = line[static_cast<std::size_t>(column)];
    } else if (row < impl_->rows) {
        vterm_screen_get_cell(impl_->screen, VTermPos{row, column}, &raw);
    } else {
        return out;
    }
    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && raw.chars[i] != 0; ++i) {
        if (raw.chars[i] != static_cast<std::uint32_t>(-1)) appendUtf8(out.text, raw.chars[i]);
    }
    out.width = raw.width;
    // The right half of a wide character reports no text; mark it with width 0.
    if (column > 0 && out.text.empty()) {
        TerminalCell left = cell(row, column - 1);
        if (left.width == 2) out.width = 0;
    }
    out.fg = convertColor(raw.fg);
    out.bg = convertColor(raw.bg);
    out.bold = raw.attrs.bold;
    out.italic = raw.attrs.italic;
    out.underline = raw.attrs.underline != 0;
    out.reverse = raw.attrs.reverse;
    return out;
}

int TerminalScreen::scrollbackLines() const { return static_cast<int>(impl_->scrollback.size()); }

// Gives the text of one row without trailing blanks.
std::string TerminalScreen::lineText(int row) const {
    std::string out;
    for (int c = 0; c < impl_->columns; ++c) {
        TerminalCell cl = cell(row, c);
        if (cl.width == 0) continue;
        out += cl.text.empty() ? std::string(" ") : cl.text;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

int TerminalScreen::cursorRow() const {
    VTermPos pos{};
    vterm_state_get_cursorpos(vterm_obtain_state(impl_->vt), &pos);
    return pos.row;
}

int TerminalScreen::cursorColumn() const {
    VTermPos pos{};
    vterm_state_get_cursorpos(vterm_obtain_state(impl_->vt), &pos);
    return pos.col;
}

bool TerminalScreen::cursorVisible() const { return impl_->cursorVisible; }
bool TerminalScreen::alternateScreen() const { return impl_->altScreen; }
MouseMode TerminalScreen::mouseMode() const { return impl_->mouse; }

// libvterm does not report bracketed paste, so a test paste shows whether it would add the markers.
bool TerminalScreen::bracketedPaste() const {
    impl_->probing = true;
    impl_->probeBytes.clear();
    vterm_keyboard_start_paste(impl_->vt);
    vterm_keyboard_end_paste(impl_->vt);
    impl_->probing = false;
    return !impl_->probeBytes.empty();
}

// Sends typed characters.
void TerminalScreen::sendText(const std::u32string& text, KeyMods mods) {
    for (char32_t c : text) vterm_keyboard_unichar(impl_->vt, static_cast<std::uint32_t>(c), convertMods(mods));
}

// Sends a special key such as an arrow or a function key.
void TerminalScreen::sendKey(TerminalKey key, KeyMods mods) {
    VTermKey vkey = VTERM_KEY_NONE;
    switch (key) {
        case TerminalKey::Enter: vkey = VTERM_KEY_ENTER; break;
        case TerminalKey::Tab: vkey = VTERM_KEY_TAB; break;
        case TerminalKey::Backspace: vkey = VTERM_KEY_BACKSPACE; break;
        case TerminalKey::Escape: vkey = VTERM_KEY_ESCAPE; break;
        case TerminalKey::Up: vkey = VTERM_KEY_UP; break;
        case TerminalKey::Down: vkey = VTERM_KEY_DOWN; break;
        case TerminalKey::Left: vkey = VTERM_KEY_LEFT; break;
        case TerminalKey::Right: vkey = VTERM_KEY_RIGHT; break;
        case TerminalKey::Insert: vkey = VTERM_KEY_INS; break;
        case TerminalKey::Delete: vkey = VTERM_KEY_DEL; break;
        case TerminalKey::Home: vkey = VTERM_KEY_HOME; break;
        case TerminalKey::End: vkey = VTERM_KEY_END; break;
        case TerminalKey::PageUp: vkey = VTERM_KEY_PAGEUP; break;
        case TerminalKey::PageDown: vkey = VTERM_KEY_PAGEDOWN; break;
        default: {
            int n = static_cast<int>(key) - static_cast<int>(TerminalKey::F1) + 1;
            vkey = static_cast<VTermKey>(VTERM_KEY_FUNCTION(n));
            break;
        }
    }
    vterm_keyboard_key(impl_->vt, vkey, convertMods(mods));
}

// Sends pasted text. The paste markers are added only when the program turned them on.
void TerminalScreen::sendPaste(const std::string& utf8) {
    vterm_keyboard_start_paste(impl_->vt);
    if (impl_->onOutput) impl_->onOutput(utf8.data(), utf8.size());
    vterm_keyboard_end_paste(impl_->vt);
}

void TerminalScreen::sendMouseMove(int row, int column, KeyMods mods) {
    vterm_mouse_move(impl_->vt, row, column, convertMods(mods));
}

void TerminalScreen::sendMouseButton(int button, bool pressed, KeyMods mods) {
    vterm_mouse_button(impl_->vt, button, pressed, convertMods(mods));
}

void TerminalScreen::setDamageCallback(std::function<void(int, int)> callback) { impl_->onDamage = std::move(callback); }

void TerminalScreen::setOutputCallback(std::function<void(const char*, std::size_t)> callback) {
    impl_->onOutput = std::move(callback);
}

}  // namespace plynth::terminal
