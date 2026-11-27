#include "plynth/core/HelpPages.h"

#include "plynth/core/ProjectFiles.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

const std::array<HelpPageEntry, 10> kPages = {{
    {"getting-started", "Getting started", "GETTING_STARTED.md", HelpPageKind::File, true},
    {"user-guide", "User guide", "USER_GUIDE.md", HelpPageKind::File, true},
    {"shortcuts", "Keyboard shortcuts", "SHORTCUTS.md", HelpPageKind::File, true},
    {"cli-guide", "CLI developer guide", "CLI_GUIDE.md", HelpPageKind::File, true},
    {"ai-guide", "AI integration guide", "AI_INTEGRATION.md", HelpPageKind::File, true},
    {"generators", "Writing generators", "GENERATORS.md", HelpPageKind::File, true},
    {"compatibility", "Opening old projects", "COMPATIBILITY.md", HelpPageKind::File, true},
    {"about", "About and licenses", "", HelpPageKind::About, true},
    {"setup-check", "Setup check", "", HelpPageKind::SetupCheck, true},
    {"setup", "Setup", "SETUP.md", HelpPageKind::File, false},
}};

constexpr std::size_t kSnippetBytes = 160;

// Lower case for ASCII letters. Other bytes stay, so UTF-8 text is not broken.
char lowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// The text in ASCII lower case.
std::string lowerText(std::string_view text)
{
    std::string out(text);
    for (char& c : out) c = lowerAscii(c);
    return out;
}

// Reads a whole file. False when it cannot be opened.
bool readFile(const fs::path& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return !in.bad();
}

// Splits text into lines without their line breaks.
std::vector<std::string_view> splitLines(std::string_view text)
{
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        if (end == text.size()) break;
        start = end + 1;
    }
    return lines;
}

// The text without spaces at both ends.
std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    return text;
}

// Says whether a line opens or closes a code fence.
bool isFence(std::string_view line)
{
    const std::string_view t = trim(line);
    return t.starts_with("```") || t.starts_with("~~~");
}

// The level and text of an ATX heading line, or level 0 when the line is not a heading.
int headingOf(std::string_view line, std::string& text)
{
    std::size_t indent = 0;
    while (indent < line.size() && indent < 3 && line[indent] == ' ') ++indent;
    line.remove_prefix(indent);
    int level = 0;
    while (level < static_cast<int>(line.size()) && line[static_cast<std::size_t>(level)] == '#') ++level;
    if (level == 0 || level > 6) return 0;
    std::string_view rest = line.substr(static_cast<std::size_t>(level));
    if (!rest.empty() && rest.front() != ' ' && rest.front() != '\t') return 0;
    rest = trim(rest);
    while (!rest.empty() && rest.back() == '#') rest.remove_suffix(1);
    text = std::string(trim(rest));
    return level;
}

// Turns "[text](target)" into "text", so anchors and snippets read like the shown page.
std::string plainLinks(std::string_view text)
{
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '[') {
            const std::size_t close = text.find(']', i + 1);
            if (close != std::string_view::npos && close + 1 < text.size() && text[close + 1] == '(') {
                const std::size_t end = text.find(')', close + 2);
                if (end != std::string_view::npos) {
                    out.append(text.substr(i + 1, close - i - 1));
                    i = end + 1;
                    continue;
                }
            }
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

// The text cut to at most kSnippetBytes bytes, never inside a UTF-8 character.
std::string cutSnippet(std::string_view text)
{
    text = trim(text);
    if (text.size() <= kSnippetBytes) return std::string(text);
    std::size_t end = kSnippetBytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
    return std::string(text.substr(0, end)) + "...";
}

// Splits search text into lower case words.
std::vector<std::string> searchWords(std::string_view text)
{
    std::vector<std::string> words;
    std::string word;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!word.empty()) words.push_back(std::move(word));
            word.clear();
        } else {
            word.push_back(lowerAscii(c));
        }
    }
    if (!word.empty()) words.push_back(std::move(word));
    return words;
}

// Says whether every word is inside the lower case line.
bool holdsAll(const std::string& lowerLine, const std::vector<std::string>& words)
{
    return std::all_of(words.begin(), words.end(),
                       [&](const std::string& w) { return lowerLine.find(w) != std::string::npos; });
}

// Says whether a path starts with base, comparing names without case, and gives the part after it.
bool insideFolder(const fs::path& base, const fs::path& path, fs::path& rest)
{
    const fs::path b = base.lexically_normal();
    const fs::path p = path.lexically_normal();
    auto bi = b.begin();
    auto pi = p.begin();
    for (; bi != b.end(); ++bi, ++pi) {
        if (bi->empty()) continue;
        if (pi == p.end()) return false;
        if (lowerText(pathToUtf8(*bi)) != lowerText(pathToUtf8(*pi))) return false;
    }
    rest.clear();
    for (; pi != p.end(); ++pi) {
        if (pi->empty()) continue;
        if (*pi == "..") return false;
        rest /= *pi;
    }
    return true;
}

// A link that is refused.
HelpLink notFound(std::string_view target)
{
    HelpLink link;
    link.status = fail(ErrorCode::NotFound, "The link \"" + std::string(target) + "\" does not lead to a help page.");
    return link;
}

}

// The rows of the Help list.
std::span<const HelpPageEntry> helpPages()
{
    return kPages;
}

// Finds a row by id.
const HelpPageEntry* helpPage(std::string_view id)
{
    for (const HelpPageEntry& page : kPages) {
        if (page.id == id) return &page;
    }
    return nullptr;
}

// Lists the headings of Markdown text, skipping code fences and giving repeated anchors a number.
std::vector<HelpHeading> helpHeadings(std::string_view markdown)
{
    std::vector<HelpHeading> headings;
    std::map<std::string, int> seen;
    bool inFence = false;
    int lineNumber = 0;
    for (std::string_view line : splitLines(markdown)) {
        ++lineNumber;
        if (isFence(line)) {
            inFence = !inFence;
            continue;
        }
        if (inFence) continue;
        std::string text;
        const int level = headingOf(line, text);
        if (level == 0) continue;
        std::string anchor = helpAnchor(text);
        const int count = seen[anchor]++;
        if (count > 0) anchor += "-" + std::to_string(count);
        headings.push_back({plainLinks(text), anchor, level, lineNumber});
    }
    return headings;
}

// Reads one page file and its headings.
HelpPageRead readHelpPage(const fs::path& docs, std::string_view id)
{
    HelpPageRead out;
    const HelpPageEntry* page = helpPage(id);
    if (!page) {
        out.status = fail(ErrorCode::NotFound, "There is no help page \"" + std::string(id) + "\".");
        return out;
    }
    if (page->kind != HelpPageKind::File) {
        out.status = fail(ErrorCode::NotFound, "The page \"" + page->title + "\" is built into the app.");
        return out;
    }
    const fs::path file = docs / pathFromUtf8(page->file);
    std::error_code ec;
    if (docs.empty() || !fs::is_regular_file(file, ec)) {
        out.status = fail(ErrorCode::NotFound, "The help page " + page->file + " is missing.");
        return out;
    }
    if (!readFile(file, out.markdown)) {
        out.status = fail(ErrorCode::CannotRead, "The help page " + page->file + " could not be read.");
        out.markdown.clear();
        return out;
    }
    out.headings = helpHeadings(out.markdown);
    return out;
}

// Searches every page with a file, heading hits first.
std::vector<HelpHit> searchHelp(const fs::path& docs, std::string_view text, std::size_t limit)
{
    const std::vector<std::string> words = searchWords(text);
    std::vector<HelpHit> headingHits;
    std::vector<HelpHit> lineHits;
    if (words.empty() || limit == 0) return {};
    for (const HelpPageEntry& page : kPages) {
        if (page.kind != HelpPageKind::File) continue;
        const HelpPageRead read = readHelpPage(docs, page.id);
        if (!read.status.ok()) continue;
        std::size_t next = 0;
        std::string heading;
        std::string anchor;
        bool inFence = false;
        int lineNumber = 0;
        for (std::string_view line : splitLines(read.markdown)) {
            ++lineNumber;
            const bool fence = isFence(line);
            if (fence) inFence = !inFence;
            const bool isHeading = !inFence && !fence && next < read.headings.size() &&
                                   read.headings[next].line == lineNumber;
            if (isHeading) {
                heading = read.headings[next].text;
                anchor = read.headings[next].anchor;
                ++next;
            }
            if (fence || trim(line).empty()) continue;
            const std::string shown = isHeading ? heading : plainLinks(line);
            if (!holdsAll(lowerText(shown), words)) continue;
            HelpHit hit{page.id, page.title, heading, anchor, cutSnippet(shown)};
            (isHeading ? headingHits : lineHits).push_back(std::move(hit));
        }
    }
    headingHits.insert(headingHits.end(), lineHits.begin(), lineHits.end());
    if (headingHits.size() > limit) headingHits.resize(limit);
    return headingHits;
}

// Makes an anchor the way GitHub does: lower case, letters, digits, "-" and "_" kept, spaces to "-".
std::string helpAnchor(std::string_view heading)
{
    const std::string plain = plainLinks(heading);
    std::string out;
    for (char c : plain) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u >= 0x80) {
            out.push_back(c);
        } else if (std::isalnum(u) || c == '-' || c == '_') {
            out.push_back(lowerAscii(c));
        } else if (c == ' ') {
            out.push_back('-');
        }
    }
    return out;
}

// Resolves a link to a page and anchor, a license file or a web address.
HelpLink resolveHelpLink(const fs::path& docs, const fs::path& licenses, std::string_view fromPage,
                         std::string_view target)
{
    HelpLink link;
    const std::string lower = lowerText(target);
    if (lower.starts_with("https://")) {
        link.url = std::string(target);
        return link;
    }
    if (target.empty()) return notFound(target);
    if (target.front() == '#') {
        if (!helpPage(fromPage)) return notFound(target);
        link.page = std::string(fromPage);
        link.anchor = std::string(target.substr(1));
        return link;
    }
    if (lower.find(':') != std::string::npos || target.front() == '/' || target.front() == '\\') {
        return notFound(target);
    }

    std::string_view pathPart = target;
    std::string anchor;
    if (const std::size_t hash = target.find('#'); hash != std::string_view::npos) {
        pathPart = target.substr(0, hash);
        anchor = std::string(target.substr(hash + 1));
    }
    const fs::path relative = pathFromUtf8(pathPart);
    if (relative.has_root_name() || relative.has_root_directory()) return notFound(target);

    const bool fromLicenses = fromPage == "about";
    const fs::path base = fromLicenses ? licenses : docs;
    if (base.empty()) return notFound(target);
    const fs::path full = (base / relative).lexically_normal();

    fs::path rest;
    if (!docs.empty() && insideFolder(docs, full, rest)) {
        if (rest.has_parent_path()) return notFound(target);
        const std::string name = lowerText(pathToUtf8(rest));
        for (const HelpPageEntry& page : kPages) {
            if (page.kind == HelpPageKind::File && lowerText(page.file) == name) {
                link.page = page.id;
                link.anchor = anchor;
                return link;
            }
        }
        return notFound(target);
    }
    if (!licenses.empty() && insideFolder(licenses, full, rest) && !rest.empty()) {
        std::error_code ec;
        if (!fs::is_regular_file(full, ec)) return notFound(target);
        link.licenseFile = full;
        link.anchor = anchor;
        return link;
    }
    return notFound(target);
}

}
