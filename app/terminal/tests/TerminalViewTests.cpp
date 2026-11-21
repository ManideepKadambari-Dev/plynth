#include <catch2/catch_test_macros.hpp>

#include <QImage>
#include <QKeyEvent>
#include <QShortcutEvent>

#include "TerminalSession.h"
#include "TerminalView.h"
#include "TestApp.h"

using namespace plynth::terminal;

namespace {

TerminalPalette testPalette() {
    TerminalPalette p;
    p.background = QColor(0x0E, 0x11, 0x16);
    p.foreground = QColor(0xE6, 0xEA, 0xF0);
    p.cursor = QColor(0xFF, 0xB4, 0x54);
    p.selection = QColor(0xFF, 0xB4, 0x54, 90);
    p.scrollIndicator = QColor(0x9A, 0xA6, 0xB5);
    for (auto& c : p.ansi) c = QColor(0x40, 0x40, 0x40);
    p.ansi[1] = QColor(0xE0, 0x20, 0x20);
    p.ansi[4] = QColor(0x20, 0x40, 0xE0);
    p.padding = 0;
    p.pixelSize = 14;
    return p;
}

// Makes sure the application object exists before any widget is built.
struct AppFirst {
    AppFirst() { testApp(); }
};

struct Fixture : AppFirst {
    TerminalSession session{24, 80};
    TerminalPalette palette = testPalette();
    TerminalView view{&session, palette};

    Fixture() {
        view.setFixedSize(view.sizeForCells(80, 24));
    }
    QImage draw() { return view.grab().toImage(); }
    QRect cellRect(int row, int col) const {
        return QRect(col * view.cellSize().width(), row * view.cellSize().height(), view.cellSize().width(),
                     view.cellSize().height());
    }
};

}  // namespace

TEST_CASE("the view draws colored cells and the cursor", "[terminal][view]") {
    Fixture f;
    // Red text on the default background, then a blue background cell, then plain text.
    f.session.feedOutput("\x1b[31mMMM\x1b[0m\x1b[44m \x1b[0mab");
    QImage image = f.draw();
    REQUIRE(image.width() == f.view.width());

    QRect textCell = f.cellRect(0, 1);
    bool foundRed = false;
    for (int y = textCell.top(); y <= textCell.bottom() && !foundRed; ++y) {
        for (int x = textCell.left(); x <= textCell.right(); ++x) {
            if (image.pixelColor(x, y) == f.palette.ansi[1]) {
                foundRed = true;
                break;
            }
        }
    }
    CHECK(foundRed);

    QRect blueCell = f.cellRect(0, 3);
    CHECK(image.pixelColor(blueCell.left() + 2, blueCell.top() + 2) == f.palette.ansi[4]);

    QRect background = f.cellRect(5, 5);
    CHECK(image.pixelColor(background.left() + 2, background.top() + 2) == f.palette.background);

    // The cursor sits after "ab", at column 6.
    QRect cursorCell = f.cellRect(0, 6);
    CHECK(image.pixelColor(cursorCell.left(), cursorCell.top()) == f.palette.cursor);
    CHECK(image.pixelColor(cursorCell.left() + 1, cursorCell.top() + 1) != f.palette.cursor);
}

TEST_CASE("a focused cursor is a filled block", "[terminal][view]") {
    Fixture f;
    f.view.show();
    f.view.activateWindow();
    f.view.setFocus();
    f.session.feedOutput("x");
    if (!f.view.hasFocus()) SKIP("the offscreen platform gave no focus");
    QImage image = f.draw();
    QRect cursorCell = f.cellRect(0, 1);
    CHECK(image.pixelColor(cursorCell.left() + 2, cursorCell.top() + 2) == f.palette.cursor);
}

TEST_CASE("the view takes shortcut keys and sends control codes", "[terminal][view]") {
    Fixture f;
    std::string sent;
    f.session.screen().setOutputCallback([&](const char* b, std::size_t n) { sent.append(b, n); });

    for (int key : {Qt::Key_S, Qt::Key_Z, Qt::Key_O}) {
        QKeyEvent override(QEvent::ShortcutOverride, key, Qt::ControlModifier);
        override.ignore();
        QCoreApplication::sendEvent(&f.view, &override);
        CHECK(override.isAccepted());
    }
    QKeyEvent toggle(QEvent::ShortcutOverride, Qt::Key_QuoteLeft, Qt::ControlModifier);
    toggle.ignore();
    QCoreApplication::sendEvent(&f.view, &toggle);
    CHECK_FALSE(toggle.isAccepted());

    QKeyEvent ctrlS(QEvent::KeyPress, Qt::Key_S, Qt::ControlModifier, QString(QChar(0x13)));
    QCoreApplication::sendEvent(&f.view, &ctrlS);
    CHECK(sent == "\x13");
    sent.clear();
    QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QCoreApplication::sendEvent(&f.view, &up);
    CHECK(sent == "\x1b[A");
    sent.clear();
    QKeyEvent letter(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
    QCoreApplication::sendEvent(&f.view, &letter);
    CHECK(sent == "a");
}

TEST_CASE("the view scrolls back through history", "[terminal][view]") {
    Fixture f;
    for (int i = 0; i < 60; ++i) f.session.feedOutput(QByteArray("line ") + QByteArray::number(i) + "\r\n");
    CHECK(f.session.screen().scrollbackLines() > 30);
    f.view.scrollBackBy(10);
    CHECK(f.view.scrollOffset() == 10);
    f.view.scrollBackBy(-100);
    CHECK(f.view.scrollOffset() == 0);
}

TEST_CASE("the view resizes the grid from its pixel size", "[terminal][view]") {
    Fixture f;
    f.view.setFixedSize(f.view.sizeForCells(100, 30));
    f.view.grab();
    CHECK(f.session.screen().columns() == 100);
    CHECK(f.session.screen().rows() == 30);
}
