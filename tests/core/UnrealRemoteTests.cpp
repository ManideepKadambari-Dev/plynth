#include "plynth/core/UnrealRemote.h"

#include <catch2/catch_test_macros.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using namespace plynth;
using namespace std::chrono_literals;

namespace {

// The tests talk only to their own group and ports, never to Unreal's default ones or the headless check's.
constexpr const char* kTestGroup = "239.0.0.77";

// A test endpoint on its own port, so tests running side by side never answer each other.
UnrealEndpoint testEndpoint(int port)
{
    UnrealEndpoint endpoint;
    endpoint.group = kTestGroup;
    endpoint.port = port;
    REQUIRE(endpoint.group != kUnrealMulticastGroup);
    REQUIRE(endpoint.port != kUnrealMulticastPort);
    REQUIRE(endpoint.port != 6866);
    return endpoint;
}

// Waits up to the given time for something to become true.
bool waitFor(const std::function<bool()>& done, std::chrono::milliseconds limit)
{
    const auto until = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < until) {
        if (done()) return true;
        std::this_thread::sleep_for(20ms);
    }
    return done();
}

// Sets one environment variable for a test and puts the old value back after.
class ScopedVariable {
public:
    ScopedVariable(const wchar_t* name, const wchar_t* value) : name_(name)
    {
        wchar_t buffer[512];
        const DWORD length = GetEnvironmentVariableW(name, buffer, 512);
        if (length > 0 && length < 512) old_ = std::wstring(buffer, length);
        SetEnvironmentVariableW(name, value);
    }
    ~ScopedVariable() { SetEnvironmentVariableW(name_, old_ ? old_->c_str() : nullptr); }

private:
    const wchar_t* name_;
    std::optional<std::wstring> old_;
};

// How the fake editor behaves once Plynth asks it to connect.
enum class FakeMode {
    Answer,       // connects, reads the command and answers in two parts
    NeverConnect, // answers pings but never connects
    HangUp,       // connects, reads the command and closes without an answer
    HugeAnswer,   // connects and answers with more than the size limit
    Silent,       // connects, reads the command and never answers
    PongToOther,  // answers pings, but addressed to another program
};

// Opens a socket on the group the way the editor does. INVALID_SOCKET when it fails.
SOCKET openFakeGroupSocket(const UnrealEndpoint& endpoint, sockaddr_in& group)
{
    SOCKET udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp == INVALID_SOCKET) return udp;
    const BOOL reuse = TRUE;
    setsockopt(udp, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    in_addr local{};
    inet_pton(AF_INET, "127.0.0.1", &local);
    group = {};
    group.sin_family = AF_INET;
    group.sin_port = htons(static_cast<u_short>(endpoint.port));
    inet_pton(AF_INET, endpoint.group.c_str(), &group.sin_addr);
    sockaddr_in bindTo = group;
    bindTo.sin_addr = local;
    const DWORD loop = 1;
    const DWORD ttl = 0;
    ip_mreq membership{};
    membership.imr_multiaddr = group.sin_addr;
    membership.imr_interface = local;
    if (bind(udp, reinterpret_cast<const sockaddr*>(&bindTo), sizeof(bindTo)) != 0 ||
        setsockopt(udp, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<const char*>(&loop), sizeof(loop)) != 0 ||
        setsockopt(udp, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof(ttl)) != 0 ||
        setsockopt(udp, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&local), sizeof(local)) != 0 ||
        setsockopt(udp, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&membership),
                   sizeof(membership)) != 0) {
        closesocket(udp);
        return INVALID_SOCKET;
    }
    return udp;
}

// True when the socket has data within the wait.
bool readableWithin(SOCKET target, std::chrono::milliseconds wait)
{
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(target, &readable);
    timeval limit{0, static_cast<long>(wait.count() * 1000)};
    return select(0, &readable, nullptr, nullptr, &limit) > 0;
}

// Sends all of a text, stopping quietly when the other side has gone.
void sendQuietly(SOCKET target, const std::string& text)
{
    std::size_t at = 0;
    while (at < text.size()) {
        const int sent = send(target, text.data() + at, static_cast<int>(text.size() - at), 0);
        if (sent <= 0) return;
        at += static_cast<std::size_t>(sent);
    }
}

// A stand in for a running Unreal Editor with remote execution on, speaking the same protocol on loopback.
class FakeEditor {
public:
    FakeEditor(UnrealEndpoint endpoint, std::string nodeId, std::string project, FakeMode mode)
        : endpoint_(std::move(endpoint)), nodeId_(std::move(nodeId)), project_(std::move(project)), mode_(mode)
    {
        WSADATA data{};
        started_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        if (started_) udp_ = openFakeGroupSocket(endpoint_, group_);
        if (udp_ != INVALID_SOCKET) thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
    }
    ~FakeEditor()
    {
        thread_.request_stop();
        if (thread_.joinable()) thread_.join();
        if (udp_ != INVALID_SOCKET) closesocket(udp_);
        if (started_) WSACleanup();
    }
    FakeEditor(const FakeEditor&) = delete;
    FakeEditor& operator=(const FakeEditor&) = delete;

    // True when the editor's socket is open and it is listening.
    bool ready() const { return udp_ != INVALID_SOCKET; }

    // The details this editor sends in its pong.
    Json nodeInfo() const
    {
        return Json{{"user", "tester"},
                    {"machine", "TESTPC"},
                    {"engine_version", "5.8.0-test"},
                    {"engine_root", "C:/Fake/UE_5.8/Engine/"},
                    {"project_root", "C:/Fake/Projects/" + project_ + "/"},
                    {"project_name", project_}};
    }

    // The node Plynth would find for this editor.
    UnrealEditorNode node() const { return unrealNodeFromPong(nodeId_, nodeInfo()).node; }

    // The last command message that came in, if any.
    std::optional<UnrealMessageRead> lastCommand()
    {
        std::lock_guard lock(mutex_);
        return lastCommand_;
    }
    // How many connection requests came in.
    int opens() const { return opens_; }
    // True once a close message came in.
    bool sawClose() const { return sawClose_; }

private:
    // Answers discovery and connection messages until stopped.
    void run(std::stop_token stop)
    {
        std::vector<char> buffer(65536);
        while (!stop.stop_requested()) {
            if (!readableWithin(udp_, 50ms)) continue;
            const int got = recv(udp_, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (got <= 0) continue;
            const std::string_view bytes(buffer.data(), static_cast<std::size_t>(got));
            const UnrealMessageRead message = parseUnrealMessage(bytes);
            if (!message.status.ok() || message.source == nodeId_) continue;
            if (!message.dest.empty() && message.dest != nodeId_) continue;
            if (message.type == "ping") {
                const std::string dest = mode_ == FakeMode::PongToOther ? "another-program" : message.source;
                sendToGroup(unrealMessage("pong", nodeId_, dest, nodeInfo()));
            } else if (message.type == "open_connection") {
                ++opens_;
                if (mode_ != FakeMode::NeverConnect) serve(message, stop);
            } else if (message.type == "close_connection") {
                sawClose_ = true;
            }
        }
    }

    // Sends one message to the group.
    void sendToGroup(const Json& message)
    {
        const std::string text = message.dump();
        sendto(udp_, text.data(), static_cast<int>(text.size()), 0, reinterpret_cast<const sockaddr*>(&group_),
               sizeof(group_));
    }

    // Connects to Plynth's command port, reads one command and answers it the way the mode says.
    void serve(const UnrealMessageRead& request, std::stop_token stop)
    {
        if (!request.data.is_object() || !request.data.contains("command_port")) return;
        SOCKET channel = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(static_cast<u_short>(request.data.value("command_port", 0)));
        inet_pton(AF_INET, request.data.value("command_ip", "127.0.0.1").c_str(), &to.sin_addr);
        if (connect(channel, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) != 0) {
            closesocket(channel);
            return;
        }
        const DWORD sendLimit = 5000;
        setsockopt(channel, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&sendLimit), sizeof(sendLimit));

        std::string text;
        std::optional<UnrealMessageRead> command;
        const auto until = std::chrono::steady_clock::now() + 5s;
        char chunk[4096];
        while (!command && !stop.stop_requested() && std::chrono::steady_clock::now() < until) {
            if (!readableWithin(channel, 50ms)) continue;
            const int got = recv(channel, chunk, sizeof(chunk), 0);
            if (got <= 0) break;
            text.append(chunk, static_cast<std::size_t>(got));
            if (Json::accept(text)) command = parseUnrealMessage(text);
        }
        if (!command) {
            closesocket(channel);
            return;
        }
        {
            std::lock_guard lock(mutex_);
            lastCommand_ = command;
        }

        if (mode_ == FakeMode::Answer) {
            const Json data = {{"success", true},
                               {"command", command->data.value("command", "")},
                               {"result", "None"},
                               {"output", Json::array({Json{{"type", "Info"}, {"output", "imported 3 meshes"}},
                                                       Json{{"type", "Warning"}, {"output", "one texture resized"}}})}};
            const std::string answer = unrealMessage("command_result", nodeId_, command->source, data).dump();
            const std::size_t half = answer.size() / 2;
            sendQuietly(channel, answer.substr(0, half));
            std::this_thread::sleep_for(100ms);
            sendQuietly(channel, answer.substr(half));
        } else if (mode_ == FakeMode::HugeAnswer) {
            const Json data = {{"success", true},
                               {"command", "x"},
                               {"result", std::string(kUnrealMaxMessageBytes + 1024, 'a')},
                               {"output", Json::array()}};
            sendQuietly(channel, unrealMessage("command_result", nodeId_, command->source, data).dump());
        } else if (mode_ == FakeMode::Silent) {
            while (!stop.stop_requested()) {
                if (!readableWithin(channel, 50ms)) continue;
                if (recv(channel, chunk, sizeof(chunk), 0) <= 0) break;
            }
        }
        if (mode_ != FakeMode::HangUp) {
            // Waits for Plynth to close first, as the real editor keeps the connection until asked to close.
            const auto closeBy = std::chrono::steady_clock::now() + 3s;
            while (!stop.stop_requested() && std::chrono::steady_clock::now() < closeBy) {
                if (!readableWithin(channel, 50ms)) continue;
                if (recv(channel, chunk, sizeof(chunk), 0) <= 0) break;
            }
        }
        closesocket(channel);
    }

    UnrealEndpoint endpoint_;
    std::string nodeId_;
    std::string project_;
    FakeMode mode_;
    bool started_ = false;
    SOCKET udp_ = INVALID_SOCKET;
    sockaddr_in group_{};
    std::mutex mutex_;
    std::optional<UnrealMessageRead> lastCommand_;
    std::atomic<int> opens_{0};
    std::atomic<bool> sawClose_{false};
    std::jthread thread_;
};

// The milliseconds a call took.
template <typename Call>
long long timed(Call call)
{
    const auto start = std::chrono::steady_clock::now();
    call();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

}

TEST_CASE("Unreal messages carry the protocol's version, magic and fields", "[core][remote_execution]")
{
    const Json ping = unrealMessage("ping", "me");
    CHECK(ping.dump() == R"({"version":1,"magic":"ue_py","type":"ping","source":"me"})");

    const Json opened = unrealMessage("open_connection", "me", "them", Json{{"command_ip", "127.0.0.1"}});
    CHECK(opened.dump() == R"({"version":1,"magic":"ue_py","type":"open_connection","source":"me","dest":"them",)"
                           R"("data":{"command_ip":"127.0.0.1"}})");
    CHECK_FALSE(unrealMessage("close_connection", "me", "them", Json::object()).contains("data"));

    const UnrealMessageRead read = parseUnrealMessage(opened.dump());
    REQUIRE(read.status.ok());
    CHECK(read.type == "open_connection");
    CHECK(read.source == "me");
    CHECK(read.dest == "them");
    CHECK(read.data["command_ip"] == "127.0.0.1");

    CHECK(parseUnrealMessage(R"({"version":1.0,"magic":"ue_py","type":"ping","source":"a"})").status.ok());
    CHECK(parseUnrealMessage("not json").status.code == ErrorCode::BadFile);
    CHECK(parseUnrealMessage("[1]").status.code == ErrorCode::BadFile);
    CHECK(parseUnrealMessage(R"({"version":2,"magic":"ue_py","type":"ping","source":"a"})").status.code ==
          ErrorCode::BadFile);
    CHECK(parseUnrealMessage(R"({"version":1,"magic":"ue_js","type":"ping","source":"a"})").status.code ==
          ErrorCode::BadFile);
    CHECK(parseUnrealMessage(R"({"version":1,"magic":"ue_py","source":"a"})").status.code == ErrorCode::BadFile);
    CHECK(parseUnrealMessage(R"({"version":1,"magic":"ue_py","type":"ping"})").status.code == ErrorCode::BadFile);
    CHECK(parseUnrealMessage(R"({"version":1,"magic":"ue_py","type":"ping","source":"a","dest":5})").status.code ==
          ErrorCode::BadFile);

    CHECK(unrealExecModeName(UnrealExecMode::ExecuteFile) == "ExecuteFile");
    CHECK(unrealExecModeName(UnrealExecMode::ExecuteStatement) == "ExecuteStatement");
    CHECK(unrealExecModeName(UnrealExecMode::EvaluateStatement) == "EvaluateStatement");

    CHECK(unrealSourceId().size() == 36);
    CHECK(unrealSourceId()[14] == '4');
    CHECK(&unrealSourceId() == &unrealSourceId());
}

TEST_CASE("A pong gives the editor's node id, project and engine version", "[core][remote_execution]")
{
    const Json data = {{"user", "sam"},
                       {"machine", "DESK"},
                       {"engine_version", "5.8.0-1234+++UE5+Release-5.8"},
                       {"engine_root", "C:/Program Files/Epic Games/UE_5.8/Engine/"},
                       {"project_root", "D:/Games/MyGame/"},
                       {"project_name", "MyGame"}};
    const UnrealNodeRead read = unrealNodeFromPong("node-1", data);
    REQUIRE(read.status.ok());
    CHECK(read.node.nodeId == "node-1");
    CHECK(read.node.user == "sam");
    CHECK(read.node.machine == "DESK");
    CHECK(read.node.engineVersion == "5.8.0-1234+++UE5+Release-5.8");
    CHECK(read.node.engineRoot == std::filesystem::path("C:/Program Files/Epic Games/UE_5.8/Engine/"));
    CHECK(read.node.projectRoot == std::filesystem::path("D:/Games/MyGame/"));
    CHECK(read.node.projectName == "MyGame");

    Json noProject = data;
    noProject.erase("project_root");
    noProject.erase("project_name");
    const UnrealNodeRead bare = unrealNodeFromPong("node-2", noProject);
    REQUIRE(bare.status.ok());
    CHECK(bare.node.projectName.empty());
    CHECK(bare.node.projectRoot.empty());

    Json noUser = data;
    noUser.erase("user");
    CHECK(unrealNodeFromPong("node-3", noUser).status.code == ErrorCode::BadFile);
    Json numberVersion = data;
    numberVersion["engine_version"] = 5.8;
    const UnrealNodeRead wrong = unrealNodeFromPong("node-4", numberVersion);
    CHECK(wrong.status.code == ErrorCode::BadFile);
    CHECK(wrong.status.where == "/data/engine_version");
    CHECK(unrealNodeFromPong("", data).status.code == ErrorCode::BadFile);
    CHECK(unrealNodeFromPong("node-5", Json()).status.code == ErrorCode::BadFile);
}

TEST_CASE("The multicast endpoint can be set for a private editor", "[core][remote_execution]")
{
    {
        ScopedVariable unset(L"PLYNTH_UNREAL_MULTICAST", nullptr);
        const UnrealEndpointRead read = unrealEndpoint();
        REQUIRE(read.status.ok());
        CHECK(read.endpoint.group == "239.0.0.1");
        CHECK(read.endpoint.port == 6766);
    }
    {
        ScopedVariable set(L"PLYNTH_UNREAL_MULTICAST", L"239.0.0.1:6866");
        const UnrealEndpointRead read = unrealEndpoint();
        REQUIRE(read.status.ok());
        CHECK(read.endpoint.group == "239.0.0.1");
        CHECK(read.endpoint.port == 6866);
    }
    for (const wchar_t* bad : {L"239.0.0.1", L"239.0.0.1:", L"239.0.0.1:70000", L"239.0.0.1:68a6", L"10.0.0.1:6866",
                               L"239.0.0:6866", L"239.0.0.256:6866", L"localhost:6866", L" 239.0.0.1:6866"}) {
        ScopedVariable set(L"PLYNTH_UNREAL_MULTICAST", bad);
        CHECK(unrealEndpoint().status.code == ErrorCode::InvalidValue);
    }
}

TEST_CASE("A bad endpoint is refused before anything is sent", "[core][remote_execution]")
{
    UnrealEndpoint notGroup;
    notGroup.group = "127.0.0.1";
    notGroup.port = 6905;
    CHECK(findUnrealEditors(notGroup, 100ms).status.code == ErrorCode::InvalidValue);
    UnrealEndpoint badPort = testEndpoint(6905);
    badPort.port = 0;
    CHECK(findUnrealEditors(badPort, 100ms).status.code == ErrorCode::InvalidValue);

    UnrealEditorNode editor;
    editor.nodeId = "someone";
    CHECK(runUnrealCommand(notGroup, editor, "print(1)", UnrealExecMode::ExecuteStatement, 1s).status.code ==
          ErrorCode::InvalidValue);
    CHECK(runUnrealCommand(testEndpoint(6905), UnrealEditorNode{}, "print(1)", UnrealExecMode::ExecuteStatement, 1s)
              .status.code == ErrorCode::InvalidValue);
}

TEST_CASE("No editor running gives an empty list and no error", "[core][remote_execution]")
{
    const UnrealDiscovery found = findUnrealEditors(testEndpoint(6901), 400ms);
    CHECK(found.status.ok());
    CHECK(found.editors.empty());
}

TEST_CASE("Two running editors are both found, each once", "[core][remote_execution]")
{
    const UnrealEndpoint endpoint = testEndpoint(6902);
    FakeEditor first(endpoint, "node-first", "Cottage", FakeMode::Answer);
    FakeEditor second(endpoint, "node-second", "Castle", FakeMode::Answer);
    REQUIRE(first.ready());
    REQUIRE(second.ready());

    // Longer than one ping round, so each editor answers more than once.
    const UnrealDiscovery found = findUnrealEditors(endpoint, 1500ms);
    REQUIRE(found.status.ok());
    REQUIRE(found.editors.size() == 2);
    const auto has = [&](const UnrealEditorNode& node) {
        return std::count(found.editors.begin(), found.editors.end(), node) == 1;
    };
    CHECK(has(first.node()));
    CHECK(has(second.node()));
    CHECK(first.node().projectName == "Cottage");
    CHECK(first.node().engineVersion == "5.8.0-test");
}

TEST_CASE("A pong meant for another program is left out", "[core][remote_execution]")
{
    const UnrealEndpoint endpoint = testEndpoint(6903);
    FakeEditor other(endpoint, "node-other", "Elsewhere", FakeMode::PongToOther);
    REQUIRE(other.ready());
    const UnrealDiscovery found = findUnrealEditors(endpoint, 600ms);
    CHECK(found.status.ok());
    CHECK(found.editors.empty());
}

TEST_CASE("A command runs in the editor and its answer is read", "[core][remote_execution]")
{
    const UnrealEndpoint endpoint = testEndpoint(6904);
    FakeEditor editor(endpoint, "node-run", "Cottage", FakeMode::Answer);
    REQUIRE(editor.ready());

    const std::string command = "\"C:/Plynth/unreal/plynth_link.py\" \"D:/Work/unreal_job.json\"";
    const UnrealCommandResult result =
        runUnrealCommand(endpoint, editor.node(), command, UnrealExecMode::ExecuteFile, 5s);
    REQUIRE(result.status.ok());
    CHECK(result.success);
    CHECK(result.result == "None");
    CHECK(result.output == std::vector<std::string>{"Info: imported 3 meshes", "Warning: one texture resized"});

    const std::optional<UnrealMessageRead> sent = editor.lastCommand();
    REQUIRE(sent);
    CHECK(sent->type == "command");
    CHECK(sent->source == unrealSourceId());
    CHECK(sent->dest == "node-run");
    CHECK(sent->data["command"] == command);
    CHECK(sent->data["unattended"] == true);
    CHECK(sent->data["exec_mode"] == "ExecuteFile");
    CHECK(waitFor([&] { return editor.sawClose(); }, 2s));
}

TEST_CASE("An editor that never connects ends with engine not found", "[core][remote_execution]")
{
    const UnrealEndpoint endpoint = testEndpoint(6906);
    FakeEditor editor(endpoint, "node-shy", "Cottage", FakeMode::NeverConnect);
    REQUIRE(editor.ready());

    UnrealCommandResult result;
    const long long took = timed([&] {
        result = runUnrealCommand(endpoint, editor.node(), "print(1)", UnrealExecMode::ExecuteStatement, 5s);
    });
    CHECK(result.status.code == ErrorCode::EngineNotFound);
    CHECK(took >= 9000);
    CHECK(took < 15000);
    // The request is sent again while waiting, as Unreal's own client does.
    CHECK(editor.opens() >= 2);
    CHECK(waitFor([&] { return editor.sawClose(); }, 2s));
}

TEST_CASE("A connection that breaks before the answer ends with engine failed", "[core][remote_execution]")
{
    const UnrealEndpoint endpoint = testEndpoint(6907);
    FakeEditor editor(endpoint, "node-hangup", "Cottage", FakeMode::HangUp);
    REQUIRE(editor.ready());

    const UnrealCommandResult result =
        runUnrealCommand(endpoint, editor.node(), "print(1)", UnrealExecMode::ExecuteStatement, 5s);
    CHECK(result.status.code == ErrorCode::EngineFailed);
    CHECK_FALSE(result.success);
    CHECK(editor.lastCommand());
}

TEST_CASE("An answer larger than the limit is not read", "[core][remote_execution]")
{
    const UnrealEndpoint endpoint = testEndpoint(6908);
    FakeEditor editor(endpoint, "node-huge", "Cottage", FakeMode::HugeAnswer);
    REQUIRE(editor.ready());

    const UnrealCommandResult result =
        runUnrealCommand(endpoint, editor.node(), "print(1)", UnrealExecMode::ExecuteStatement, 10s);
    CHECK(result.status.code == ErrorCode::EngineFailed);
    CHECK(result.status.message.find("larger") != std::string::npos);
    CHECK(result.result.empty());
}

TEST_CASE("An editor that never answers ends with engine failed after the timeout", "[core][remote_execution]")
{
    const UnrealEndpoint endpoint = testEndpoint(6909);
    FakeEditor editor(endpoint, "node-silent", "Cottage", FakeMode::Silent);
    REQUIRE(editor.ready());

    UnrealCommandResult result;
    const long long took = timed([&] {
        result = runUnrealCommand(endpoint, editor.node(), "print(1)", UnrealExecMode::ExecuteStatement, 1s);
    });
    CHECK(result.status.code == ErrorCode::EngineFailed);
    CHECK(took >= 900);
    CHECK(took < 6000);
}

TEST_CASE("A command can be stopped while it waits for the answer", "[core][remote_execution]")
{
    const UnrealEndpoint endpoint = testEndpoint(6910);
    FakeEditor editor(endpoint, "node-wait", "Cottage", FakeMode::Silent);
    REQUIRE(editor.ready());

    std::stop_source stopper;
    std::jthread later([&] {
        std::this_thread::sleep_for(1500ms);
        stopper.request_stop();
    });
    UnrealCommandResult result;
    const long long took = timed([&] {
        result = runUnrealCommand(endpoint, editor.node(), "print(1)", UnrealExecMode::ExecuteStatement, 60s,
                                  stopper.get_token());
    });
    CHECK(result.status.code == ErrorCode::Cancelled);
    CHECK(took < 5000);
    CHECK(editor.lastCommand());
    CHECK(waitFor([&] { return editor.sawClose(); }, 2s));
}

TEST_CASE("Looking for editors can be stopped", "[core][remote_execution]")
{
    std::stop_source stopper;
    std::jthread later([&] {
        std::this_thread::sleep_for(200ms);
        stopper.request_stop();
    });
    UnrealDiscovery found;
    const long long took = timed([&] { found = findUnrealEditors(testEndpoint(6911), 20s, stopper.get_token()); });
    CHECK(found.status.code == ErrorCode::Cancelled);
    CHECK(took < 3000);

    std::stop_source already;
    already.request_stop();
    UnrealEditorNode editor;
    editor.nodeId = "nobody";
    CHECK(runUnrealCommand(testEndpoint(6911), editor, "print(1)", UnrealExecMode::ExecuteStatement, 5s,
                           already.get_token())
              .status.code == ErrorCode::Cancelled);
}
