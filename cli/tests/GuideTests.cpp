#include "TestSupport.h"

#include <plynth/cli/Cli.h>
#include <plynth/core/Status.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using plynth::cli::testing::runChecked;

namespace {

// The lines of a file under docs/.
std::vector<std::string> docLines(const std::string& name) {
    std::ifstream in(std::filesystem::path(PLYNTH_DOCS_DIR) / name, std::ios::binary);
    REQUIRE(in);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

// The text of the guide's headings of level three and four, without the hashes.
std::vector<std::string> headings(const std::vector<std::string>& lines) {
    std::vector<std::string> out;
    for (const std::string& line : lines) {
        if (line.rfind("### ", 0) == 0) {
            out.push_back(line.substr(4));
        } else if (line.rfind("#### ", 0) == 0) {
            out.push_back(line.substr(5));
        }
    }
    return out;
}

// Splits words the way a terminal does for these examples: spaces separate words, double quotes keep spaces.
std::vector<std::string> shellWords(const std::string& text) {
    std::vector<std::string> words;
    std::string word;
    bool quoted = false;
    bool any = false;
    for (const char c : text) {
        if (c == '"') {
            quoted = !quoted;
            any = true;
        } else if (c == ' ' && !quoted) {
            if (any) {
                words.push_back(word);
            }
            word.clear();
            any = false;
        } else {
            word += c;
            any = true;
        }
    }
    if (any) {
        words.push_back(word);
    }
    return words;
}

// Every "plynth ..." line inside a code block that is not JSON, with its line number. Lines with a placeholder
// such as <id> are left out.
std::vector<std::pair<int, std::string>> examples(const std::vector<std::string>& lines) {
    std::vector<std::pair<int, std::string>> out;
    bool inBlock = false;
    bool json = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.rfind("```", 0) == 0) {
            json = !inBlock && line.substr(3) == "json";
            inBlock = !inBlock;
            continue;
        }
        if (!inBlock || json || line.rfind("plynth ", 0) != 0) {
            continue;
        }
        if (line.find('<') != std::string::npos || line.find("...") != std::string::npos) {
            continue;
        }
        out.emplace_back(static_cast<int>(i + 1), line.substr(7));
    }
    return out;
}

// Says whether a heading names a command: its words, or for "materials group add, group set" the group words
// first and the rest later in the heading.
bool named(const std::vector<std::string>& texts, const std::string& command) {
    const std::size_t firstSpace = command.find(' ');
    const std::size_t secondSpace = firstSpace == std::string::npos ? std::string::npos : command.find(' ', firstSpace + 1);
    for (const std::string& text : texts) {
        if (text.find(command) != std::string::npos) {
            return true;
        }
        if (secondSpace != std::string::npos && text.rfind(command.substr(0, secondSpace), 0) == 0 &&
            text.find(command.substr(firstSpace + 1)) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}

TEST_CASE("every command help lists has a heading in the command line guide", "[guide]") {
    const std::vector<std::string> texts = headings(docLines("CLI_GUIDE.md"));
    const auto outcome = runChecked({"help"});
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        const std::string words = command.at("command").get<std::string>();
        INFO(words);
        CHECK(named(texts, words));
    }
}

TEST_CASE("every error code and exit code is in the guide's tables", "[guide]") {
    std::ostringstream text;
    for (const std::string& line : docLines("CLI_GUIDE.md")) {
        text << line << '\n';
    }
    const std::string guide = text.str();
    const auto outcome = runChecked({"help"});
    for (const auto& entry : outcome.answer.at("result").at("error_codes")) {
        const std::string code = entry.at("code").get<std::string>();
        INFO(code);
        CHECK(guide.find("| `" + code + "` |") != std::string::npos);
    }
    for (const auto& entry : outcome.answer.at("result").at("exit_codes")) {
        const std::string row = "| " + std::to_string(entry.at("code").get<int>()) + " |";
        INFO(row);
        CHECK(guide.find(row) != std::string::npos);
    }
}

TEST_CASE("every plynth example in both guides parses as a real command", "[guide]") {
    int checked = 0;
    for (const char* name : {"CLI_GUIDE.md", "AI_INTEGRATION.md"}) {
        for (const auto& [line, text] : examples(docLines(name))) {
            const std::vector<std::string> args = shellWords(text);
            const std::string problem = plynth::cli::parseProblem(args);
            INFO(name << " line " << line << ": plynth " << text);
            CHECK(problem.empty());
            ++checked;
        }
    }
    CHECK(checked > 100);
}

TEST_CASE("no page but the AI guide speaks of AI", "[guide]") {
    const std::regex words("\\bAI\\b|coding assistant|language model|chatbot", std::regex::icase);
    for (const auto& entry : std::filesystem::directory_iterator(PLYNTH_DOCS_DIR)) {
        if (entry.path().extension() != ".md" || entry.path().filename() == "AI_INTEGRATION.md") {
            continue;
        }
        const std::vector<std::string> lines = docLines(entry.path().filename().string());
        for (std::size_t i = 0; i < lines.size(); ++i) {
            INFO(entry.path().filename().string() << " line " << i + 1 << ": " << lines[i]);
            // The help pages may name the AI guide by its title.
            const bool title = lines[i].find("AI_INTEGRATION.md") != std::string::npos ||
                               lines[i].find("AI integration guide") != std::string::npos;
            CHECK((title || !std::regex_search(lines[i], words)));
        }
    }
}
