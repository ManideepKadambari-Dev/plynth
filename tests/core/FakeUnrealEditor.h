#pragma once

#include <plynth/core/UnrealRemote.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace plynth::test {

// The multicast group the link tests use, never Unreal's default one or the headless check's.
inline constexpr const char* kFakeEditorGroup = "239.0.0.77";

// What the fake editor does with a command: returns the output lines it prints, as (type, text) pairs.
using FakeEditorScript = std::function<std::vector<std::pair<std::string, std::string>>(const std::string& command)>;

// A stand in for a running Unreal Editor with remote execution on, on loopback only. It answers pings with its
// project and runs every command it gets through the script given, then answers with success and its output.
class FakeUnrealEditor {
public:
    FakeUnrealEditor(int port, std::string nodeId, std::string project, FakeEditorScript script)
        : nodeId_(std::move(nodeId)), project_(std::move(project)), script_(std::move(script))
    {
        endpoint_.group = kFakeEditorGroup;
        endpoint_.port = port;
        WSADATA data{};
        started_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        if (started_) udp_ = openGroupSocket();
        if (udp_ != INVALID_SOCKET) thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
    }
    ~FakeUnrealEditor()
    {
        thread_.request_stop();
        if (thread_.joinable()) thread_.join();
        if (udp_ != INVALID_SOCKET) closesocket(udp_);
        if (started_) WSACleanup();
    }
    FakeUnrealEditor(const FakeUnrealEditor&) = delete;
    FakeUnrealEditor& operator=(const FakeUnrealEditor&) = delete;

    // True when the editor's socket is open and it is listening.
    bool ready() const { return udp_ != INVALID_SOCKET; }
    // The folder of its project as its pong gives it.
    std::string projectRoot() const { return "C:/Fake/Projects/" + project_ + "/"; }
    // How many commands it ran.
    int commands() const { return commands_; }
    // The text of the last command it ran.
    std::string lastCommand()
    {
        std::lock_guard lock(mutex_);
        return lastCommand_;
    }

private:
    // The details this editor sends in its pong.
    Json nodeInfo() const
    {
        return Json{{"user", "tester"},
                    {"machine", "TESTPC"},
                    {"engine_version", "5.8.0-test"},
                    {"engine_root", "C:/Fake/UE_5.8/Engine/"},
                    {"project_root", projectRoot()},
                    {"project_name", project_}};
    }

    // Opens a socket on the group the way the editor does. INVALID_SOCKET when it fails.
    SOCKET openGroupSocket()
    {
        SOCKET udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (udp == INVALID_SOCKET) return udp;
        const BOOL reuse = TRUE;
        setsockopt(udp, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        in_addr local{};
        inet_pton(AF_INET, "127.0.0.1", &local);
        group_ = {};
        group_.sin_family = AF_INET;
        group_.sin_port = htons(static_cast<u_short>(endpoint_.port));
        inet_pton(AF_INET, endpoint_.group.c_str(), &group_.sin_addr);
        sockaddr_in bindTo = group_;
        bindTo.sin_addr = local;
        const DWORD loop = 1;
        const DWORD ttl = 0;
        ip_mreq membership{};
        membership.imr_multiaddr = group_.sin_addr;
        membership.imr_interface = local;
        if (bind(udp, reinterpret_cast<const sockaddr*>(&bindTo), sizeof(bindTo)) != 0 ||
            setsockopt(udp, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<const char*>(&loop), sizeof(loop)) != 0 ||
            setsockopt(udp, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof(ttl)) != 0 ||
            setsockopt(udp, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&local), sizeof(local)) != 0 ||
            setsockopt(udp, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&membership), sizeof(membership)) != 0) {
            closesocket(udp);
            return INVALID_SOCKET;
        }
        return udp;
    }

    // True when the socket has data within the wait.
    static bool readableWithin(SOCKET target, std::chrono::milliseconds wait)
    {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(target, &readable);
        timeval limit{0, static_cast<long>(wait.count() * 1000)};
        return select(0, &readable, nullptr, nullptr, &limit) > 0;
    }

    // Sends all of a text, stopping quietly when the other side has gone.
    static void sendQuietly(SOCKET target, const std::string& text)
    {
        std::size_t at = 0;
        while (at < text.size()) {
            const int sent = send(target, text.data() + at, static_cast<int>(text.size() - at), 0);
            if (sent <= 0) return;
            at += static_cast<std::size_t>(sent);
        }
    }

    // Answers discovery and connection messages until stopped.
    void run(std::stop_token stop)
    {
        std::vector<char> buffer(65536);
        while (!stop.stop_requested()) {
            if (!readableWithin(udp_, std::chrono::milliseconds(50))) continue;
            const int got = recv(udp_, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (got <= 0) continue;
            const UnrealMessageRead message = parseUnrealMessage(std::string_view(buffer.data(), static_cast<std::size_t>(got)));
            if (!message.status.ok() || message.source == nodeId_) continue;
            if (!message.dest.empty() && message.dest != nodeId_) continue;
            if (message.type == "ping") {
                const std::string text = unrealMessage("pong", nodeId_, message.source, nodeInfo()).dump();
                sendto(udp_, text.data(), static_cast<int>(text.size()), 0, reinterpret_cast<const sockaddr*>(&group_), sizeof(group_));
            } else if (message.type == "open_connection") {
                serve(message, stop);
            }
        }
    }

    // Connects to Plynth's command port, runs one command through the script and answers it.
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
        std::string text;
        std::optional<UnrealMessageRead> command;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        char chunk[4096];
        while (!command && !stop.stop_requested() && std::chrono::steady_clock::now() < until) {
            if (!readableWithin(channel, std::chrono::milliseconds(50))) continue;
            const int got = recv(channel, chunk, sizeof(chunk), 0);
            if (got <= 0) break;
            text.append(chunk, static_cast<std::size_t>(got));
            if (Json::accept(text)) command = parseUnrealMessage(text);
        }
        if (!command) {
            closesocket(channel);
            return;
        }
        const std::string commandText = command->data.value("command", "");
        {
            std::lock_guard lock(mutex_);
            lastCommand_ = commandText;
        }
        ++commands_;
        Json output = Json::array();
        if (script_) {
            for (const auto& [type, line] : script_(commandText)) output.push_back(Json{{"type", type}, {"output", line}});
        }
        const Json data = {{"success", true}, {"command", commandText}, {"result", "None"}, {"output", std::move(output)}};
        sendQuietly(channel, unrealMessage("command_result", nodeId_, command->source, data).dump());
        const auto closeBy = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!stop.stop_requested() && std::chrono::steady_clock::now() < closeBy) {
            if (!readableWithin(channel, std::chrono::milliseconds(50))) continue;
            if (recv(channel, chunk, sizeof(chunk), 0) <= 0) break;
        }
        closesocket(channel);
    }

    UnrealEndpoint endpoint_;
    std::string nodeId_;
    std::string project_;
    FakeEditorScript script_;
    bool started_ = false;
    SOCKET udp_ = INVALID_SOCKET;
    sockaddr_in group_{};
    std::mutex mutex_;
    std::string lastCommand_;
    std::atomic<int> commands_{0};
    std::jthread thread_;
};

}
