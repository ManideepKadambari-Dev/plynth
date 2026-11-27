#pragma once

#include "Status.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// Talking to a running Unreal Editor through Unreal's own Python remote execution (owner, 2026-11-27). The
// user's project needs the Python Editor Script Plugin with "Enable Remote Execution" on; nothing else is
// installed into Unreal. Windows sockets, no extra library. The protocol is the one of Unreal's
// remote_execution.py (Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python/remote_execution.py):
//
// Every message is one UTF-8 JSON object {"version": 1, "magic": "ue_py", "type", "source", "dest", "data"},
// with dest and data left out when not used. source is this process's node id, a random UUID text made once.
//   discovery  UDP multicast to group:port (default 239.0.0.1:6766, TTL 0, bound to 127.0.0.1, loopback on).
//              Plynth sends "ping"; every editor answers "pong" to Plynth's node with data {"user", "machine",
//              "engine_version", "engine_root", "project_root", "project_name"}. The last two are left out when no
//              project is loaded. The editor's node id is the pong's source.
//   connect    Plynth listens on a TCP port of 127.0.0.1 (any free one), then sends "open_connection" to the
//              editor's node with data {"command_ip": "127.0.0.1", "command_port": <port>}; the editor connects.
//   command    over TCP, "command" with data {"command": <text>, "unattended": true, "exec_mode": <mode>};
//              the editor answers "command_result" with data {"success": bool, "command": <text>,
//              "result": <text>, "output": [{"type": "Info" | "Warning" | "Error", "output": <text>}, ...]}.
//              A message is one JSON text per send, read until it parses, at most kUnrealMaxMessageBytes.
//   close      "close_connection" by UDP to the node, then the TCP socket is closed.
// Only addresses on this computer are used: the bind address and command address are always 127.0.0.1, so no
// other machine can be reached or reach in.

inline constexpr std::string_view kUnrealMulticastGroup = "239.0.0.1";
inline constexpr int kUnrealMulticastPort = 6766;
inline constexpr std::string_view kUnrealBindAddress = "127.0.0.1";
inline constexpr std::size_t kUnrealMaxMessageBytes = 2 * 1024 * 1024;

// Sets another multicast group and port, "239.0.0.1:6866", so tests and the headless check talk to their own
// editor and never to one the user has open. Read at each call. A value that does not parse is InvalidValue and
// nothing is sent.
inline constexpr std::string_view kUnrealEndpointVariable = "PLYNTH_UNREAL_MULTICAST";

// Where discovery messages go.
struct UnrealEndpoint {
    std::string group = std::string(kUnrealMulticastGroup);
    int port = kUnrealMulticastPort;
};

// The endpoint from kUnrealEndpointVariable when set, else the default.
struct UnrealEndpointRead {
    Status status;
    UnrealEndpoint endpoint;
};
UnrealEndpointRead unrealEndpoint();

// One editor that answered a ping.
struct UnrealEditorNode {
    std::string nodeId;
    std::string user;
    std::string machine;
    std::string engineVersion;       // for example "5.8.0"
    std::filesystem::path engineRoot;
    std::filesystem::path projectRoot;
    std::string projectName;

    bool operator==(const UnrealEditorNode&) const = default;
};

struct UnrealDiscovery {
    // Ok also when none answered; NetworkFailed when the socket could not be opened; InvalidValue for a bad
    // endpoint; Cancelled with the editors found until then.
    Status status;
    std::vector<UnrealEditorNode> editors; // each node once, in the order they answered
};

// Pings and collects the answers for wait. Blocks.
UnrealDiscovery findUnrealEditors(const UnrealEndpoint& endpoint, std::chrono::milliseconds wait,
                                  std::stop_token stop = {});

// How the editor runs a command. Names on the wire: "ExecuteFile", "ExecuteStatement", "EvaluateStatement".
// ExecuteFile runs a .py file; the command is the quoted file path followed by its quoted arguments.
enum class UnrealExecMode { ExecuteFile, ExecuteStatement, EvaluateStatement };

std::string_view unrealExecModeName(UnrealExecMode mode);

struct UnrealCommandResult {
    // Ok when the editor answered, whatever success says. EngineNotFound when the node did not connect within
    // the connect wait, EngineFailed when the connection broke, the answer was larger than kUnrealMaxMessageBytes
    // or not a command result, or no answer came within the timeout, Cancelled. InvalidValue for a bad endpoint
    // or an editor without a node id, NetworkFailed when a socket could not be opened.
    Status status;
    bool success = false;           // the editor's own answer
    std::string result;
    std::vector<std::string> output; // each output line as "<Type>: <text>"
};

inline constexpr std::chrono::seconds kUnrealConnectWait{10};

// Opens a connection to one editor, runs one command and closes it. Blocks.
UnrealCommandResult runUnrealCommand(const UnrealEndpoint& endpoint, const UnrealEditorNode& editor,
                                     std::string_view command, UnrealExecMode mode, std::chrono::seconds timeout,
                                     std::stop_token stop = {});

// The message helpers, for the fake editor in the tests. unrealMessage leaves out dest and data when empty.
Json unrealMessage(std::string_view type, std::string_view source, std::string_view dest = {},
                   const Json& data = nullptr);

struct UnrealMessageRead {
    Status status; // BadFile when it is not JSON, the magic or version is wrong, or a field has the wrong type
    std::string type;
    std::string source;
    std::string dest;
    Json data;
};
UnrealMessageRead parseUnrealMessage(std::string_view text);

// The node of a pong: its source is the node id, its data the rest. BadFile when the source is empty, a required
// field is missing or a field is not text. project_root and project_name may be missing.
struct UnrealNodeRead {
    Status status;
    UnrealEditorNode node;
};
UnrealNodeRead unrealNodeFromPong(std::string_view source, const Json& data);

// This process's node id.
const std::string& unrealSourceId();

}
