#include <plynth/cli/Words.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace plynth::cli {

namespace {

constexpr std::array<std::string_view, 20> kWords = {"version", "help",    "project", "scale",  "point",    "wall",
                                                     "opening", "floor",   "room",    "detect", "session",  "settings",
                                                     "library", "assets",  "doctor",  "place",  "item",     "build",
                                                     "material", "materials"};

// Writes text as a JSON string the way the CLI's JSON writer does: quotes and control characters escaped,
// everything else as it is.
void appendQuoted(std::string& out, std::string_view text) {
    out += '"';
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(c));
                out += buffer;
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

}

std::span<const std::string_view> commandWords() {
    return kWords;
}

bool isCommandWord(std::string_view word) {
    return std::ranges::find(kWords, word) != kWords.end();
}

std::string unknownCommandMessage(std::span<const std::string> args) {
    if (args.empty()) {
        return "No command was given. Run 'plynth help' to list the commands.";
    }
    return "Unknown command '" + args.front() + "'. Run 'plynth help' to list the commands.";
}

std::string unknownCommandAnswer(std::span<const std::string> args) {
    std::vector<std::string> rest;
    bool pretty = false;
    for (const std::string& arg : args) {
        if (arg == "--pretty") {
            pretty = true;
        } else {
            rest.push_back(arg);
        }
    }
    std::string message;
    appendQuoted(message, unknownCommandMessage(rest));
    if (pretty) {
        return "{\n  \"ok\": false,\n  \"command\": \"\",\n  \"error\": {\n    \"code\": \"unknown_command\",\n"
               "    \"message\": " + message + "\n  }\n}\n";
    }
    return "{\"ok\":false,\"command\":\"\",\"error\":{\"code\":\"unknown_command\",\"message\":" + message + "}}\n";
}

}
