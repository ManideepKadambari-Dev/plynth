#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace plynth::terminal {

struct TerminalColor {
    enum class Kind { Default, Indexed, Rgb };
    Kind kind = Kind::Default;
    std::uint8_t index = 0;
    std::uint8_t r = 0, g = 0, b = 0;

    bool operator==(const TerminalColor& o) const {
        return kind == o.kind && index == o.index && r == o.r && g == o.g && b == o.b;
    }
};

struct TerminalCell {
    std::string text;          // UTF-8, empty for an empty cell or the right half of a wide character
    int width = 1;             // 2 for a wide character, 0 for its right half
    TerminalColor fg;
    TerminalColor bg;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool reverse = false;

    bool sameStyle(const TerminalCell& o) const {
        return fg == o.fg && bg == o.bg && bold == o.bold && italic == o.italic && underline == o.underline &&
               reverse == o.reverse;
    }
};

enum class MouseMode { None, Click, Drag, Move };

enum class TerminalKey {
    Enter, Tab, Backspace, Escape, Up, Down, Left, Right, Insert, Delete, Home, End, PageUp, PageDown,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12
};

struct KeyMods {
    bool shift = false;
    bool alt = false;
    bool ctrl = false;
};

// An in memory terminal screen driven by escape sequences. It holds no UI code.
class TerminalScreen {
public:
    TerminalScreen(int rows, int columns);
    ~TerminalScreen();
    TerminalScreen(const TerminalScreen&) = delete;
    TerminalScreen& operator=(const TerminalScreen&) = delete;

    void feed(const char* bytes, std::size_t size);
    void feed(const std::string& bytes) { feed(bytes.data(), bytes.size()); }
    void resize(int rows, int columns);

    int rows() const;
    int columns() const;

    // Row 0 is the top of the screen. Negative rows reach into the scrollback (-1 is the newest scrolled line).
    TerminalCell cell(int row, int column) const;
    int scrollbackLines() const;
    std::string lineText(int row) const;

    int cursorRow() const;
    int cursorColumn() const;
    bool cursorVisible() const;
    bool alternateScreen() const;
    bool bracketedPaste() const;
    MouseMode mouseMode() const;

    // Key input. The bytes the program expects come back through the output callback.
    void sendText(const std::u32string& text, KeyMods mods);
    void sendKey(TerminalKey key, KeyMods mods);
    void sendPaste(const std::string& utf8);
    void sendMouseMove(int row, int column, KeyMods mods);
    void sendMouseButton(int button, bool pressed, KeyMods mods);

    // Called with the screen rows that changed (first, last, inclusive), or (0, rows-1) for a scroll.
    void setDamageCallback(std::function<void(int firstRow, int lastRow)> callback);
    // Called with bytes the terminal sends back to the program: keys and answers to queries.
    void setOutputCallback(std::function<void(const char* bytes, std::size_t size)> callback);

    static constexpr int kMaxScrollback = 5000;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace plynth::terminal
