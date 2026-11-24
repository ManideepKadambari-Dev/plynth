#include "GuideSupport.h"

#include <plynth/core/HelpPages.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace plynth;
using plynth::test::TempFolder;
using plynth::test::writeFile;

namespace {

const char* kGettingStarted =
    "# Getting started\n"
    "\n"
    "Open the sample apartment.\n"
    "\n"
    "## Build and look\n"
    "Press Build to make the stairs.\n"
    "\n"
    "```text\n"
    "# Not a heading\n"
    "stairs inside code\n"
    "```\n"
    "\n"
    "## Build and look\n"
    "\n"
    "## What's new? (beta)\n";

const char* kUserGuide =
    "# User guide\r\n"
    "\r\n"
    "## Stairs\r\n"
    "\r\n"
    "Stairs join two floors. See [the setup page](SETUP.md).\r\n";

// A docs folder with two pages, the unlisted setup page and an architecture note.
struct Docs {
    TempFolder root;
    std::filesystem::path docs = root.path() / "docs";
    std::filesystem::path licenses = root.path() / "licenses";

    Docs()
    {
        writeFile(docs / "GETTING_STARTED.md", kGettingStarted);
        writeFile(docs / "USER_GUIDE.md", kUserGuide);
        writeFile(docs / "SETUP.md", "# Setup\n\nInstall the giraffe tools.\n");
        writeFile(docs / "architecture" / "notes.md", "# Notes\n\nzebra\n");
        writeFile(licenses / "THIRD_PARTY_NOTICES.md", "# Notices\n");
        writeFile(licenses / "texts" / "x.txt", "license text");
    }
};

}

TEST_CASE("The Help rows come in the agreed order", "[help]")
{
    const auto pages = helpPages();
    REQUIRE(pages.size() == 9);
    const char* ids[] = {"getting-started", "user-guide", "shortcuts", "cli-guide", "ai-guide",
                         "generators", "about", "setup-check", "setup"};
    const char* titles[] = {"Getting started", "User guide", "Keyboard shortcuts", "CLI developer guide",
                            "AI integration guide", "Writing generators", "About and licenses", "Setup check", "Setup"};
    for (std::size_t i = 0; i < pages.size(); ++i) {
        CHECK(pages[i].id == ids[i]);
        CHECK(pages[i].title == titles[i]);
        CHECK(pages[i].listed == (i != 8));
    }
    CHECK(pages[0].file == "GETTING_STARTED.md");
    CHECK(pages[4].file == "AI_INTEGRATION.md");
    CHECK(pages[6].kind == HelpPageKind::About);
    CHECK(pages[7].kind == HelpPageKind::SetupCheck);
    CHECK(pages[8].file == "SETUP.md");
    CHECK(helpPage("user-guide") == &pages[1]);
    CHECK(helpPage("nope") == nullptr);
}

TEST_CASE("Reading a page gives its text and headings", "[help]")
{
    Docs d;
    const HelpPageRead read = readHelpPage(d.docs, "getting-started");
    REQUIRE(read.status.ok());
    CHECK(read.markdown == kGettingStarted);
    REQUIRE(read.headings.size() == 4);
    CHECK(read.headings[0] == HelpHeading{"Getting started", "getting-started", 1, 1});
    CHECK(read.headings[1] == HelpHeading{"Build and look", "build-and-look", 2, 5});
    CHECK(read.headings[2] == HelpHeading{"Build and look", "build-and-look-1", 2, 13});
    CHECK(read.headings[3] == HelpHeading{"What's new? (beta)", "whats-new-beta", 2, 15});
}

TEST_CASE("A missing, unknown or built in page is not found", "[help]")
{
    Docs d;
    CHECK(readHelpPage(d.docs, "shortcuts").status.code == ErrorCode::NotFound);
    CHECK(readHelpPage(d.docs, "nope").status.code == ErrorCode::NotFound);
    CHECK(readHelpPage(d.docs, "about").status.code == ErrorCode::NotFound);
    CHECK(readHelpPage({}, "getting-started").status.code == ErrorCode::NotFound);
}

TEST_CASE("Anchors are made the way GitHub makes them", "[help]")
{
    CHECK(helpAnchor("Keyboard shortcuts") == "keyboard-shortcuts");
    CHECK(helpAnchor("Step 1: open the plan") == "step-1-open-the-plan");
    CHECK(helpAnchor("The `plynth build` command") == "the-plynth-build-command");
    CHECK(helpAnchor("[Link](x.md) text") == "link-text");
    CHECK(helpAnchor("snake_case and-dash") == "snake_case-and-dash");
    CHECK(helpAnchor("Grüße") == "grüße");
}

TEST_CASE("Search finds every word ignoring case, heading hits first", "[help]")
{
    Docs d;
    const auto hits = searchHelp(d.docs, "stairs");
    REQUIRE(hits.size() == 4);
    CHECK(hits[0].page == "user-guide");
    CHECK(hits[0].heading == "Stairs");
    CHECK(hits[0].anchor == "stairs");
    CHECK(hits[0].snippet == "Stairs");
    CHECK(hits[1].page == "getting-started");
    CHECK(hits[1].title == "Getting started");
    CHECK(hits[1].heading == "Build and look");
    CHECK(hits[1].anchor == "build-and-look");
    CHECK(hits[1].snippet == "Press Build to make the stairs.");
    CHECK(hits[2].snippet == "stairs inside code");
    CHECK(hits[3].page == "user-guide");
    CHECK(hits[3].snippet == "Stairs join two floors. See the setup page.");

    const auto two = searchHelp(d.docs, "BUILD   stairs");
    REQUIRE(two.size() == 1);
    CHECK(two[0].snippet == "Press Build to make the stairs.");
}

TEST_CASE("Search keeps to the limit and may find nothing", "[help]")
{
    Docs d;
    CHECK(searchHelp(d.docs, "stairs", 2).size() == 2);
    CHECK(searchHelp(d.docs, "stairs", 0).empty());
    CHECK(searchHelp(d.docs, "nothing-like-this").empty());
    CHECK(searchHelp(d.docs, "   ").empty());
}

TEST_CASE("Search covers the unlisted setup page but never docs/architecture", "[help]")
{
    Docs d;
    const auto giraffe = searchHelp(d.docs, "giraffe");
    REQUIRE(giraffe.size() == 1);
    CHECK(giraffe[0].page == "setup");
    CHECK(searchHelp(d.docs, "zebra").empty());
}

TEST_CASE("Snippets are cut on a character boundary", "[help]")
{
    TempFolder root;
    std::string line = "x";
    for (int i = 0; i < 100; ++i) line += "\xC3\xA9";
    writeFile(root.path() / "GETTING_STARTED.md", "# Long\n\n" + line + "\n");
    const auto hits = searchHelp(root.path(), "x");
    REQUIRE(hits.size() == 1);
    const std::string& snippet = hits[0].snippet;
    REQUIRE(snippet.size() > 3);
    CHECK(snippet.ends_with("..."));
    const std::string cut = snippet.substr(0, snippet.size() - 3);
    CHECK(cut.size() <= 160);
    CHECK(cut.size() % 2 == 1);
    CHECK(line.starts_with(cut));
}

TEST_CASE("Links resolve to pages, anchors, license files and web addresses", "[help]")
{
    Docs d;
    HelpLink link = resolveHelpLink(d.docs, d.licenses, "getting-started", "USER_GUIDE.md#stairs");
    REQUIRE(link.status.ok());
    CHECK(link.page == "user-guide");
    CHECK(link.anchor == "stairs");

    link = resolveHelpLink(d.docs, d.licenses, "getting-started", "#build-and-look");
    REQUIRE(link.status.ok());
    CHECK(link.page == "getting-started");
    CHECK(link.anchor == "build-and-look");

    link = resolveHelpLink(d.docs, d.licenses, "user-guide", "SETUP.md");
    REQUIRE(link.status.ok());
    CHECK(link.page == "setup");

    link = resolveHelpLink(d.docs, d.licenses, "user-guide", "https://www.blender.org/download/");
    REQUIRE(link.status.ok());
    CHECK(link.url == "https://www.blender.org/download/");
    CHECK(link.page.empty());

    link = resolveHelpLink(d.docs, d.licenses, "user-guide", "../licenses/texts/x.txt");
    REQUIRE(link.status.ok());
    CHECK(link.licenseFile.filename() == "x.txt");

    link = resolveHelpLink(d.docs, d.licenses, "about", "texts/x.txt");
    REQUIRE(link.status.ok());
    CHECK(link.licenseFile.filename() == "x.txt");
}

TEST_CASE("Links outside docs and licenses are refused", "[help]")
{
    Docs d;
    for (const char* target : {"../secret.md", "../../x.md", "C:/Windows/win.ini", "C:x.md", "/etc/passwd",
                               "\\\\server\\share\\x.md", "file:///C:/x.md", "http://example.org",
                               "architecture/notes.md", "MISSING.md", "../licenses/texts/missing.txt", ""}) {
        INFO(target);
        CHECK(resolveHelpLink(d.docs, d.licenses, "getting-started", target).status.code == ErrorCode::NotFound);
    }
}
