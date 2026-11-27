#include <HelpRender.h>
#include <HelpWindow.h>

#include <plynth/core/HelpPages.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextFragment>

#include <filesystem>
#include <memory>
#include <string>

using namespace plynth;
using namespace plynth::guide;

namespace {

#ifdef NDEBUG
constexpr qint64 kOpenLimitMs = 2000;
constexpr qint64 kDoubleLimitMs = 4000;
#else
constexpr qint64 kOpenLimitMs = 20000;
constexpr qint64 kDoubleLimitMs = 40000;
#endif

// One shared application object for all tests, drawn offscreen so no window appears.
QApplication& testApp()
{
    static int argc = 1;
    static char name[] = "plynth_help_view_tests";
    static char* argv[] = {name, nullptr};
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    static QApplication instance(argc, argv);
    return instance;
}

// The docs folder of the source tree.
std::filesystem::path docsFolder()
{
    return plynth::pathFromUtf8(PLYNTH_DOCS_DIR);
}

// The licenses folder of the source tree.
std::filesystem::path licensesFolder()
{
    return plynth::pathFromUtf8(PLYNTH_LICENSES_DIR);
}

// A style with plain fonts and sizes, as the app would fill it from its theme.
GuideStyle testStyle()
{
    GuideStyle style;
    style.ground = QColor(0x0E, 0x11, 0x16);
    style.panel = QColor(0x16, 0x1A, 0x21);
    style.raised = QColor(0x1E, 0x23, 0x2C);
    style.line = QColor(0x2A, 0x31, 0x3C);
    style.text = QColor(0xE6, 0xEA, 0xF0);
    style.muted = QColor(0x9A, 0xA6, 0xB5);
    style.blue = QColor(0x4C, 0x8D, 0xFF);
    style.amber = QColor(0xFF, 0xB4, 0x54);
    style.onBlue = QColor(0xFF, 0xFF, 0xFF);
    style.uiFont = QFont(QStringLiteral("Segoe UI"), 10);
    style.titleFont = QFont(QStringLiteral("Segoe UI"), 14);
    style.monoFont = QFont(QStringLiteral("Consolas"), 10);
    style.unit = 4;
    style.radius = 4;
    style.listWidth = 240;
    return style;
}

// The page view of a Help window.
QTextBrowser* pageView(HelpWindow& window)
{
    for (QTextBrowser* browser : window.findChildren<QTextBrowser*>()) {
        if (browser->objectName() == QStringLiteral("helpPage")) return browser;
    }
    return nullptr;
}

// Lays out the whole document shown in a view and gives back its height.
qreal layOutAll(QTextBrowser* browser)
{
    return browser->document()->documentLayout()->documentSize().height();
}

// A Help window on the real docs that builds pages at once, sized and shown offscreen.
struct Window {
    QApplication& app = testApp();
    HelpWindow window;

    Window() : window(testStyle(), docsFolder(), licensesFolder(), QPixmap())
    {
        window.setBackgroundRendering(false);
        window.resize(1280, 800);
        window.show();
        QApplication::processEvents();
    }
};

}

TEST_CASE("Help view: every page in the help list opens", "[help-view]")
{
    Window w;
    QTextBrowser* browser = pageView(w.window);
    REQUIRE(browser);
    bool setupAsked = false;
    QObject::connect(&w.window, &HelpWindow::setupCheckRequested, [&] { setupAsked = true; });
    int files = 0;
    for (const HelpPageEntry& entry : helpPages()) {
        if (!entry.listed) continue;
        const QString id = QString::fromStdString(entry.id);
        INFO(entry.id);
        if (entry.kind == HelpPageKind::SetupCheck) {
            w.window.showPage(id);
            CHECK(setupAsked);
            continue;
        }
        w.window.showPage(id);
        CHECK(w.window.currentPage() == id);
        if (entry.kind != HelpPageKind::File) continue;
        ++files;
        REQUIRE(std::filesystem::is_regular_file(docsFolder() / plynth::pathFromUtf8(entry.file)));
        const RenderedHelpPage rendered = renderHelpPage(docsFolder(), entry.id, HelpPageLook{});
        CHECK(rendered.ok);
        CHECK_FALSE(rendered.headings.empty());
        CHECK(browser->document()->characterCount() > 200);
        CHECK(layOutAll(browser) > 0);
    }
    CHECK(files == 7);
}

TEST_CASE("Help view: the CLI guide opens in time and is built only once", "[help-view]")
{
    Window w;
    QTextBrowser* browser = pageView(w.window);
    REQUIRE(browser);
    w.window.showPage(QStringLiteral("getting-started"));

    QElapsedTimer timer;
    timer.start();
    w.window.showPage(QStringLiteral("cli-guide"));
    CHECK(layOutAll(browser) > 0);
    QApplication::processEvents();
    const qint64 took = timer.elapsed();
    INFO("Opening the CLI guide took " << took << " ms");
    CHECK(took < kOpenLimitMs);

    QTextDocument* first = browser->document();
    CHECK(first->characterCount() > 100000);
    w.window.showPage(QStringLiteral("user-guide"));
    CHECK(browser->document() != first);
    timer.restart();
    w.window.showPage(QStringLiteral("cli-guide"), QStringLiteral("error-codes"));
    CHECK(browser->document() == first);
    CHECK(timer.elapsed() < kOpenLimitMs);
}

TEST_CASE("Help view: a page twice the size of the CLI guide is built and laid out in time", "[help-view]")
{
    testApp();
    QFile file(QString::fromStdWString((docsFolder() / "CLI_GUIDE.md").wstring()));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QString once = QString::fromUtf8(file.readAll());
    REQUIRE(once.size() > 100000);
    const QString twice = once + QStringLiteral("\n\n") + once;
    const GuideStyle style = testStyle();
    const HelpPageLook look{style.uiFont, style.monoFont, 16};

    QTextBrowser browser;
    browser.resize(1000, 700);
    browser.show();
    QElapsedTimer timer;
    timer.start();
    std::unique_ptr<QTextDocument> document = renderHelpMarkdown(twice, look);
    browser.setDocument(document.get());
    CHECK(layOutAll(&browser) > 0);
    QApplication::processEvents();
    const qint64 took = timer.elapsed();
    INFO("Building and laying out twice the CLI guide took " << took << " ms");
    CHECK(took < kDoubleLimitMs);
    browser.setDocument(nullptr);
}

TEST_CASE("Help view: code is shown in the mono font", "[help-view]")
{
    testApp();
    const GuideStyle style = testStyle();
    const HelpPageLook look{style.uiFont, style.monoFont, 16};
    const std::unique_ptr<QTextDocument> document =
        renderHelpMarkdown(QStringLiteral("Run `plynth build` now.\n\n```\nplynth export\n```\n"), look);
    int mono = 0;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.charFormat().fontFixedPitch()) continue;
            CHECK(fragment.charFormat().fontFamilies().toStringList().value(0) == style.monoFont.family());
            ++mono;
        }
    }
    CHECK(mono >= 2);
}

TEST_CASE("Help view: a page built in the background is shown when it is ready", "[help-view]")
{
    testApp();
    HelpWindow window(testStyle(), docsFolder(), licensesFolder(), QPixmap());
    window.resize(1280, 800);
    window.show();
    QTextBrowser* browser = pageView(window);
    REQUIRE(browser);
    window.showPage(QStringLiteral("cli-guide"));
    QElapsedTimer timer;
    timer.start();
    while (browser->document()->characterCount() < 100000 && timer.elapsed() < kDoubleLimitMs) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    CHECK(window.currentPage() == QStringLiteral("cli-guide"));
    CHECK(browser->document()->characterCount() > 100000);
}
