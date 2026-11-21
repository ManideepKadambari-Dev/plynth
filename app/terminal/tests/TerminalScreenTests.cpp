#include <catch2/catch_test_macros.hpp>

#include <string>

#include "TerminalScreen.h"

using namespace plynth::terminal;

namespace {

TerminalColor indexed(int i) {
    TerminalColor c;
    c.kind = TerminalColor::Kind::Indexed;
    c.index = static_cast<std::uint8_t>(i);
    return c;
}

}  // namespace

TEST_CASE("plain text lands in the right cells", "[terminal][screen]") {
    TerminalScreen screen(24, 80);
    screen.feed("hello\r\nworld");
    CHECK(screen.cell(0, 0).text == "h");
    CHECK(screen.cell(0, 4).text == "o");
    CHECK(screen.cell(0, 5).text.empty());
    CHECK(screen.cell(1, 0).text == "w");
    CHECK(screen.lineText(1) == "world");
    CHECK(screen.cursorRow() == 1);
    CHECK(screen.cursorColumn() == 5);
    CHECK(screen.cursorVisible());
}

TEST_CASE("colors and attributes", "[terminal][screen]") {
    TerminalScreen screen(24, 80);
    screen.feed("\x1b[31mR\x1b[0m\x1b[38;2;1;2;3mT\x1b[0m\x1b[1;3;4;7mS\x1b[0mN");
    CHECK(screen.cell(0, 0).fg == indexed(1));
    TerminalCell rgb = screen.cell(0, 1);
    CHECK(rgb.fg.kind == TerminalColor::Kind::Rgb);
    CHECK(rgb.fg.r == 1);
    CHECK(rgb.fg.g == 2);
    CHECK(rgb.fg.b == 3);
    TerminalCell styled = screen.cell(0, 2);
    CHECK(styled.bold);
    CHECK(styled.italic);
    CHECK(styled.underline);
    CHECK(styled.reverse);
    TerminalCell plain = screen.cell(0, 3);
    CHECK(plain.fg.kind == TerminalColor::Kind::Default);
    CHECK_FALSE(plain.bold);
}

TEST_CASE("cursor moves with escape sequences", "[terminal][screen]") {
    TerminalScreen screen(24, 80);
    screen.feed("\x1b[5;10H");
    CHECK(screen.cursorRow() == 4);
    CHECK(screen.cursorColumn() == 9);
    screen.feed("X");
    CHECK(screen.cell(4, 9).text == "X");
    screen.feed("\x1b[?25l");
    CHECK_FALSE(screen.cursorVisible());
    screen.feed("\x1b[?25h");
    CHECK(screen.cursorVisible());
}

TEST_CASE("alternate screen switches and restores", "[terminal][screen]") {
    TerminalScreen screen(24, 80);
    screen.feed("main text");
    CHECK_FALSE(screen.alternateScreen());
    screen.feed("\x1b[?1049h");
    CHECK(screen.alternateScreen());
    CHECK(screen.lineText(0).empty());
    screen.feed("[Halt text");
    CHECK(screen.lineText(0) == "alt text");
    screen.feed("\x1b[?1049l");
    CHECK_FALSE(screen.alternateScreen());
    CHECK(screen.lineText(0) == "main text");
}

TEST_CASE("resize keeps text", "[terminal][screen]") {
    TerminalScreen screen(24, 80);
    screen.feed("keep me");
    screen.resize(30, 100);
    CHECK(screen.rows() == 30);
    CHECK(screen.columns() == 100);
    CHECK(screen.lineText(0) == "keep me");
    screen.resize(10, 40);
    CHECK(screen.rows() == 10);
    CHECK(screen.lineText(0) == "keep me");
}

TEST_CASE("wide and accented characters", "[terminal][screen]") {
    TerminalScreen screen(24, 80);
    screen.feed("\xC3\xBC\xE4\xB8\xAD" "x");
    TerminalCell u = screen.cell(0, 0);
    CHECK(u.text == "\xC3\xBC");
    CHECK(u.width == 1);
    TerminalCell wide = screen.cell(0, 1);
    CHECK(wide.text == "\xE4\xB8\xAD");
    CHECK(wide.width == 2);
    CHECK(screen.cell(0, 2).width == 0);
    CHECK(screen.cell(0, 3).text == "x");
}

TEST_CASE("bracketed paste and mouse flags", "[terminal][screen]") {
    TerminalScreen screen(24, 80);
    CHECK_FALSE(screen.bracketedPaste());
    screen.feed("\x1b[?2004h");
    CHECK(screen.bracketedPaste());
    screen.feed("\x1b[?2004l");
    CHECK_FALSE(screen.bracketedPaste());

    CHECK(screen.mouseMode() == MouseMode::None);
    screen.feed("\x1b[?1000h");
    CHECK(screen.mouseMode() == MouseMode::Click);
    screen.feed("\x1b[?1000l");
    CHECK(screen.mouseMode() == MouseMode::None);
}

TEST_CASE("scrollback grows past the screen height", "[terminal][screen]") {
    TerminalScreen screen(5, 20);
    for (int i = 0; i < 12; ++i) screen.feed("line " + std::to_string(i) + "\r\n");
    CHECK(screen.scrollbackLines() >= 8);
    CHECK(screen.cell(-1, 0).text == "l");
    CHECK(screen.lineText(-screen.scrollbackLines()) == "line 0");
}

TEST_CASE("scrollback is capped", "[terminal][screen]") {
    TerminalScreen screen(5, 20);
    for (int i = 0; i < TerminalScreen::kMaxScrollback + 200; ++i) screen.feed("x\r\n");
    CHECK(screen.scrollbackLines() == TerminalScreen::kMaxScrollback);
}

TEST_CASE("keys come back through the output callback", "[terminal][screen]") {
    TerminalScreen screen(24, 80);
    std::string sent;
    screen.setOutputCallback([&](const char* bytes, std::size_t size) { sent.append(bytes, size); });
    screen.sendKey(TerminalKey::Up, {});
    CHECK(sent == "\x1b[A");
    sent.clear();
    screen.sendText(U"a", {});
    CHECK(sent == "a");
    sent.clear();
    screen.sendText(U"c", KeyMods{false, false, true});
    CHECK(sent == "\x03");
    sent.clear();
    screen.feed("\x1b[6n");
    CHECK(sent == "\x1b[1;1R");
    sent.clear();
    screen.feed("\x1b[?2004h");
    screen.sendPaste("hi");
    CHECK(sent == "\x1b[200~hi\x1b[201~");
}

TEST_CASE("damage callback reports changed rows", "[terminal][screen]") {
    TerminalScreen screen(24, 80);
    int first = 99, last = -1;
    screen.setDamageCallback([&](int f, int l) {
        first = std::min(first, f);
        last = std::max(last, l);
    });
    screen.feed("\x1b[3;1Hx");
    CHECK(first <= 2);
    CHECK(last >= 2);
}
