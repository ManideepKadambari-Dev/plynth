#pragma once

#include "plynth/core/Status.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// File: a Markdown file in docs/. About and SetupCheck are built into the app.
enum class HelpPageKind { File, About, SetupCheck };

struct HelpPageEntry {
    std::string id;
    std::string title;
    std::string file; // the file name in docs/, empty for built in pages
    HelpPageKind kind = HelpPageKind::File;
    bool listed = true; // false for pages only reached through links and search

    bool operator==(const HelpPageEntry&) const = default;
};

// The Help rows in order: getting-started, user-guide, shortcuts, cli-guide, ai-guide, generators, about,
// setup-check, then the unlisted setup page.
std::span<const HelpPageEntry> helpPages();

// The row with this id, or null.
const HelpPageEntry* helpPage(std::string_view id);

struct HelpHeading {
    std::string text;
    std::string anchor;
    int level = 1;
    int line = 0; // 1 based

    bool operator==(const HelpHeading&) const = default;
};

struct HelpPageRead {
    Status status; // NotFound for an unknown id, a built in page or a missing file; CannotRead
    std::string markdown;
    std::vector<HelpHeading> headings;
};

// Reads one page of docs/ and lists its headings. Headings inside code fences are left out.
HelpPageRead readHelpPage(const std::filesystem::path& docs, std::string_view id);

// The headings of Markdown text, with anchors made the way GitHub makes them.
std::vector<HelpHeading> helpHeadings(std::string_view markdown);

struct HelpHit {
    std::string page;    // page id
    std::string title;   // page title
    std::string heading; // the heading the hit sits under, empty before the first heading
    std::string anchor;  // that heading's anchor
    std::string snippet; // the matching line, cut to a short length on a character boundary

    bool operator==(const HelpHit&) const = default;
};

// Finds lines that hold every word of text, ignoring case. Heading hits come first, then the others in page
// order. Pages of every row that has a file are searched, also unlisted ones. At most limit hits.
std::vector<HelpHit> searchHelp(const std::filesystem::path& docs, std::string_view text, std::size_t limit = 50);

// The anchor GitHub makes from a heading: lower case, spaces to "-", punctuation dropped.
std::string helpAnchor(std::string_view heading);

struct HelpLink {
    Status status;                     // NotFound for anything outside docs/ and licenses/
    std::string page;                  // page id, empty for a license file or a web address
    std::string anchor;                // without "#"
    std::filesystem::path licenseFile; // a file inside licenses/
    std::string url;                   // an https address, never fetched
};

// Turns a link found in a page into a page id and anchor, a license file, or a web address.
// fromPage is the id of the page the link is on. ".." that leaves docs/, absolute and drive paths are refused.
HelpLink resolveHelpLink(const std::filesystem::path& docs, const std::filesystem::path& licenses,
                         std::string_view fromPage, std::string_view target);

}
