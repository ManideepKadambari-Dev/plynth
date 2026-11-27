#include "CheckEditor.h"

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

#include <chrono>
#include <stop_token>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace plynth::app {

namespace {

constexpr const char* kCheckNodeId = "plynth-check-editor";

// True when the socket has data within the wait.
bool readableWithin(SOCKET target, std::chrono::milliseconds wait)
{
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(target, &readable);
    timeval limit{0, static_cast<long>(wait.count() * 1000)};
    return select(0, &readable, nullptr, nullptr, &limit) > 0;
}

}

struct CheckEditor::State {
    std::string project;
    bool started = false;
    SOCKET udp = INVALID_SOCKET;
    sockaddr_in group{};
    std::jthread thread;

    // Joins the multicast group the way the editor does, bound to this computer only.
    bool open(const std::string& groupAddress, int port)
    {
        udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (udp == INVALID_SOCKET)
            return false;
        const BOOL reuse = TRUE;
        setsockopt(udp, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        in_addr local{};
        inet_pton(AF_INET, "127.0.0.1", &local);
        group.sin_family = AF_INET;
        group.sin_port = htons(static_cast<u_short>(port));
        if (inet_pton(AF_INET, groupAddress.c_str(), &group.sin_addr) != 1)
            return false;
        sockaddr_in bindTo = group;
        bindTo.sin_addr = local;
        const DWORD loop = 1;
        const DWORD ttl = 0;
        ip_mreq membership{};
        membership.imr_multiaddr = group.sin_addr;
        membership.imr_interface = local;
        return bind(udp, reinterpret_cast<const sockaddr*>(&bindTo), sizeof(bindTo)) == 0 &&
               setsockopt(udp, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<const char*>(&loop), sizeof(loop)) == 0 &&
               setsockopt(udp, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof(ttl)) == 0 &&
               setsockopt(udp, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&local), sizeof(local)) == 0 &&
               setsockopt(udp, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&membership),
                          sizeof(membership)) == 0;
    }

    // The details a pong carries, as an editor with this project open would send them.
    Json nodeInfo() const
    {
        return Json{{"user", "check"},
                    {"machine", "CHECKPC"},
                    {"engine_version", "5.8.0"},
                    {"engine_root", "C:/Program Files/Epic Games/UE_5.8/Engine/"},
                    {"project_root", "C:/Users/check/Documents/Unreal Projects/" + project + "/"},
                    {"project_name", project}};
    }

    // Answers every ping with a pong until stopped.
    void run(std::stop_token stop)
    {
        std::vector<char> buffer(65536);
        while (!stop.stop_requested()) {
            if (!readableWithin(udp, std::chrono::milliseconds(50)))
                continue;
            const int got = recv(udp, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (got <= 0)
                continue;
            const UnrealMessageRead message =
                parseUnrealMessage(std::string_view(buffer.data(), static_cast<std::size_t>(got)));
            if (!message.status.ok() || message.source == kCheckNodeId || message.type != "ping")
                continue;
            const std::string text = unrealMessage("pong", kCheckNodeId, message.source, nodeInfo()).dump();
            sendto(udp, text.data(), static_cast<int>(text.size()), 0, reinterpret_cast<const sockaddr*>(&group),
                   sizeof(group));
        }
    }
};

// Opens the socket and starts answering on a thread of its own.
CheckEditor::CheckEditor(const std::string& group, int port, std::string projectName) : d_(std::make_unique<State>())
{
    d_->project = std::move(projectName);
    WSADATA data{};
    d_->started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    if (!d_->started)
        return;
    if (!d_->open(group, port)) {
        if (d_->udp != INVALID_SOCKET)
            closesocket(d_->udp);
        d_->udp = INVALID_SOCKET;
        return;
    }
    State* state = d_.get();
    d_->thread = std::jthread([state](std::stop_token stop) { state->run(stop); });
}

// Stops answering and closes the socket.
CheckEditor::~CheckEditor()
{
    d_->thread.request_stop();
    if (d_->thread.joinable())
        d_->thread.join();
    if (d_->udp != INVALID_SOCKET)
        closesocket(d_->udp);
    if (d_->started)
        WSACleanup();
}

// Says whether the socket opened.
bool CheckEditor::ready() const
{
    return d_->udp != INVALID_SOCKET;
}

}
