#include "plynth/core/UnrealRemote.h"
#include "plynth/core/ProjectFiles.h"

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
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <random>
#include <utility>

namespace plynth {

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr int kProtocolVersion = 1;
constexpr std::string_view kProtocolMagic = "ue_py";
constexpr milliseconds kPingEvery{1000};
constexpr milliseconds kOpenEvery{1000};
constexpr milliseconds kSlice{100};
constexpr std::size_t kReceiveChunk = 8192;
constexpr std::size_t kDatagramBytes = 65536;

// Starts Windows sockets for one call and stops them again at the end.
struct WinsockSession {
    bool ok = false;
    WinsockSession()
    {
        WSADATA data{};
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockSession()
    {
        if (ok) WSACleanup();
    }
    WinsockSession(const WinsockSession&) = delete;
    WinsockSession& operator=(const WinsockSession&) = delete;
};

// Owns one socket and closes it when it goes away.
class Socket {
public:
    Socket() = default;
    explicit Socket(SOCKET handle) : socket_(handle) {}
    ~Socket() { reset(); }
    Socket(Socket&& other) noexcept : socket_(std::exchange(other.socket_, INVALID_SOCKET)) {}
    Socket& operator=(Socket&& other) noexcept
    {
        if (this != &other) {
            reset();
            socket_ = std::exchange(other.socket_, INVALID_SOCKET);
        }
        return *this;
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    SOCKET get() const { return socket_; }
    bool valid() const { return socket_ != INVALID_SOCKET; }
    void reset()
    {
        if (valid()) closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }

private:
    SOCKET socket_ = INVALID_SOCKET;
};

// Reads an address such as "239.0.0.1". False when it is not four numbers from 0 to 255.
bool parseIpv4(std::string_view text, in_addr& out)
{
    std::array<unsigned, 4> parts{};
    std::size_t at = 0;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            if (at >= text.size() || text[at] != '.') return false;
            ++at;
        }
        const char* begin = text.data() + at;
        const char* end = text.data() + text.size();
        if (begin == end || *begin < '0' || *begin > '9') return false;
        const auto [next, error] = std::from_chars(begin, end, parts[i]);
        if (error != std::errc{} || parts[i] > 255 || next - begin > 3) return false;
        at = static_cast<std::size_t>(next - text.data());
    }
    if (at != text.size()) return false;
    const std::uint32_t value = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    out.s_addr = htonl(value);
    return true;
}

// The address 127.0.0.1, the only one Plynth binds to or hands out.
in_addr localAddress()
{
    in_addr address{};
    parseIpv4(kUnrealBindAddress, address);
    return address;
}

// Checks that the endpoint is a multicast group and a usable port, and gives its socket address.
Status checkEndpoint(const UnrealEndpoint& endpoint, sockaddr_in& out)
{
    in_addr group{};
    if (!parseIpv4(endpoint.group, group))
        return fail(ErrorCode::InvalidValue,
                    "The Unreal multicast group \"" + endpoint.group + "\" is not an address.");
    const std::uint32_t first = ntohl(group.s_addr) >> 24;
    if (first < 224 || first > 239)
        return fail(ErrorCode::InvalidValue,
                    "The Unreal multicast group " + endpoint.group + " is not a multicast address.");
    if (endpoint.port < 1 || endpoint.port > 65535)
        return fail(ErrorCode::InvalidValue,
                    "The Unreal multicast port " + std::to_string(endpoint.port) + " is out of range.");
    out = {};
    out.sin_family = AF_INET;
    out.sin_port = htons(static_cast<u_short>(endpoint.port));
    out.sin_addr = group;
    return {};
}

// Opens the discovery socket the way Unreal's own client does: bound to 127.0.0.1 on the group's port, joined to
// the group on that address, with loopback on and a TTL of 0 so nothing leaves this computer.
Status openGroupSocket(const UnrealEndpoint& endpoint, sockaddr_in& group, Socket& out)
{
    if (Status status = checkEndpoint(endpoint, group); !status.ok()) return status;
    const auto failed = [&] {
        return fail(ErrorCode::NetworkFailed, "The Unreal discovery socket on " + std::string(kUnrealBindAddress) +
                                                  " port " + std::to_string(endpoint.port) + " could not be opened.");
    };
    Socket udp(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (!udp.valid()) return failed();
    const BOOL reuse = TRUE;
    if (setsockopt(udp.get(), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse)) != 0)
        return failed();
    const in_addr local = localAddress();
    sockaddr_in bindTo{};
    bindTo.sin_family = AF_INET;
    bindTo.sin_port = group.sin_port;
    bindTo.sin_addr = local;
    if (bind(udp.get(), reinterpret_cast<const sockaddr*>(&bindTo), sizeof(bindTo)) != 0) return failed();
    const DWORD loop = 1;
    const DWORD ttl = 0;
    ip_mreq membership{};
    membership.imr_multiaddr = group.sin_addr;
    membership.imr_interface = local;
    if (setsockopt(udp.get(), IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<const char*>(&loop), sizeof(loop)) != 0 ||
        setsockopt(udp.get(), IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof(ttl)) != 0 ||
        setsockopt(udp.get(), IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&local), sizeof(local)) != 0 ||
        setsockopt(udp.get(), IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&membership),
                   sizeof(membership)) != 0)
        return failed();
    out = std::move(udp);
    return {};
}

// The wire text of a message. Text that is not valid UTF-8 is replaced instead of stopping the send.
std::string messageText(const Json& message)
{
    return message.dump(-1, ' ', false, Json::error_handler_t::replace);
}

// Sends one message to the multicast group. False when Windows refuses it.
bool sendToGroup(const Socket& udp, const sockaddr_in& group, const Json& message)
{
    const std::string text = messageText(message);
    const int sent = sendto(udp.get(), text.data(), static_cast<int>(text.size()), 0,
                            reinterpret_cast<const sockaddr*>(&group), sizeof(group));
    return sent == static_cast<int>(text.size());
}

// Waits up to the given time for a socket to have data. 1 when it has, 0 when the time ran out, -1 on an error.
int waitReadable(const Socket& target, milliseconds wait)
{
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(target.get(), &readable);
    const long long ms = std::max<long long>(0, wait.count());
    timeval limit{static_cast<long>(ms / 1000), static_cast<long>((ms % 1000) * 1000)};
    const int ready = select(0, &readable, nullptr, nullptr, &limit);
    if (ready == SOCKET_ERROR) return -1;
    return ready > 0 ? 1 : 0;
}

// The time to wait in one round: up to the next event, at most one slice, at least one millisecond.
milliseconds sliceUntil(Clock::time_point until)
{
    const auto left = std::chrono::duration_cast<milliseconds>(until - Clock::now());
    return std::clamp(left, milliseconds{1}, kSlice);
}

// True when a message is meant for this process: not sent by it, and sent to it or to everyone.
bool forThisNode(const UnrealMessageRead& message)
{
    return message.source != unrealSourceId() && (message.dest.empty() || message.dest == unrealSourceId());
}

// Reads one text field of an object. False when it is there but not text, or missing while required.
bool readText(const Json& data, const char* name, bool required, std::string& out)
{
    const auto found = data.find(name);
    if (found == data.end() || found->is_null()) return !required;
    if (!found->is_string()) return false;
    out = found->get<std::string>();
    return true;
}

// Finds where the first whole JSON object in a stream of bytes ends, without parsing it twice.
class ObjectEnd {
public:
    // Reads more bytes. Gives the count of them that belong to the object once it is whole.
    std::optional<std::size_t> feed(std::string_view bytes)
    {
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            const char c = bytes[i];
            if (!started_) {
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
                if (c != '{') {
                    invalid_ = true;
                    return std::nullopt;
                }
                started_ = true;
                depth_ = 1;
                continue;
            }
            if (inString_) {
                if (escaped_) escaped_ = false;
                else if (c == '\\') escaped_ = true;
                else if (c == '"') inString_ = false;
                continue;
            }
            if (c == '"') inString_ = true;
            else if (c == '{' || c == '[') ++depth_;
            else if ((c == '}' || c == ']') && --depth_ == 0) return i + 1;
        }
        return std::nullopt;
    }
    bool invalid() const { return invalid_; }

private:
    int depth_ = 0;
    bool started_ = false;
    bool inString_ = false;
    bool escaped_ = false;
    bool invalid_ = false;
};

// Makes a random node id in the usual UUID text form, like Python's uuid4.
std::string makeNodeId()
{
    std::random_device random;
    std::array<unsigned char, 16> bytes{};
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        const unsigned value = random();
        for (std::size_t k = 0; k < 4; ++k) bytes[i + k] = static_cast<unsigned char>(value >> (8 * k));
    }
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);
    std::string text;
    char hex[3];
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) text += '-';
        std::snprintf(hex, sizeof(hex), "%02x", bytes[i]);
        text += hex;
    }
    return text;
}

// The text of a duration in seconds for messages, "1 second" or "10 seconds".
std::string secondsText(long long seconds)
{
    return std::to_string(seconds) + (seconds == 1 ? " second" : " seconds");
}

// Sends all of a text over the command connection. False when the connection broke.
bool sendAll(const Socket& channel, const std::string& text)
{
    std::size_t at = 0;
    while (at < text.size()) {
        const int sent = send(channel.get(), text.data() + at, static_cast<int>(text.size() - at), 0);
        if (sent == SOCKET_ERROR || sent == 0) return false;
        at += static_cast<std::size_t>(sent);
    }
    return true;
}

// Reads the editor's answer and fills in the result, or gives why it could not.
UnrealCommandResult readAnswer(const Socket& channel, std::chrono::seconds timeout, std::stop_token stop)
{
    UnrealCommandResult out;
    const auto deadline = Clock::now() + timeout;
    std::string text;
    ObjectEnd end;
    std::array<char, kReceiveChunk> chunk{};
    while (true) {
        if (stop.stop_requested()) {
            out.status = fail(ErrorCode::Cancelled, "The command to the Unreal Editor was stopped.");
            return out;
        }
        if (Clock::now() >= deadline) {
            out.status = fail(ErrorCode::EngineFailed,
                              "The Unreal Editor gave no answer within " + secondsText(timeout.count()) + ".");
            return out;
        }
        const int ready = waitReadable(channel, sliceUntil(deadline));
        if (ready < 0) {
            out.status = fail(ErrorCode::EngineFailed, "The connection to the Unreal Editor broke.");
            return out;
        }
        if (ready == 0) continue;
        const int got = recv(channel.get(), chunk.data(), static_cast<int>(chunk.size()), 0);
        if (got == 0) {
            out.status = fail(ErrorCode::EngineFailed, "The Unreal Editor closed the connection before it answered.");
            return out;
        }
        if (got < 0) {
            out.status = fail(ErrorCode::EngineFailed, "The connection to the Unreal Editor broke.");
            return out;
        }
        const std::string_view bytes(chunk.data(), static_cast<std::size_t>(got));
        const std::optional<std::size_t> used = end.feed(bytes);
        if (end.invalid()) {
            out.status = fail(ErrorCode::EngineFailed, "The Unreal Editor's answer is not a JSON message.");
            return out;
        }
        const std::size_t take = used ? *used : bytes.size();
        if (text.size() + take > kUnrealMaxMessageBytes) {
            out.status = fail(ErrorCode::EngineFailed, "The Unreal Editor's answer is larger than " +
                                                           std::to_string(kUnrealMaxMessageBytes / (1024 * 1024)) +
                                                           " MB, so it was not read.");
            return out;
        }
        text.append(bytes.substr(0, take));
        if (used) break;
    }

    const UnrealMessageRead message = parseUnrealMessage(text);
    if (!message.status.ok() || message.type != "command_result" || !forThisNode(message) ||
        !message.data.is_object()) {
        out.status = fail(ErrorCode::EngineFailed, "The Unreal Editor's answer is not a command result.");
        return out;
    }
    const Json& data = message.data;
    const auto success = data.find("success");
    const auto output = data.find("output");
    std::string ignored;
    if (success == data.end() || !success->is_boolean() || !readText(data, "result", true, out.result) ||
        !readText(data, "command", true, ignored) || output == data.end() || !output->is_array()) {
        out.result.clear();
        out.status = fail(ErrorCode::EngineFailed, "The Unreal Editor's command result is missing a field.");
        return out;
    }
    for (const Json& line : *output) {
        std::string type;
        std::string said;
        if (!line.is_object() || !readText(line, "type", true, type) || !readText(line, "output", true, said)) {
            out.result.clear();
            out.output.clear();
            out.status = fail(ErrorCode::EngineFailed, "The Unreal Editor's command output is not readable.");
            return out;
        }
        out.output.push_back(type + ": " + said);
    }
    out.success = success->get<bool>();
    return out;
}

// Waits for the editor to connect to the command port, asking it again every second.
Status acceptEditor(const Socket& udp, const sockaddr_in& group, const Socket& listener, const Json& openMessage,
                    const UnrealEditorNode& editor, std::stop_token stop, bool& openSent, Socket& channel)
{
    const auto deadline = Clock::now() + kUnrealConnectWait;
    auto nextOpen = Clock::now();
    while (true) {
        if (stop.stop_requested()) return fail(ErrorCode::Cancelled, "The command to the Unreal Editor was stopped.");
        const auto now = Clock::now();
        if (now >= deadline) {
            const std::string name = editor.projectName.empty() ? std::string("The Unreal Editor")
                                                                : "The Unreal Editor with " + editor.projectName;
            return fail(ErrorCode::EngineNotFound,
                        name + " did not connect within " + secondsText(kUnrealConnectWait.count()) + ".");
        }
        if (now >= nextOpen) {
            if (!sendToGroup(udp, group, openMessage))
                return fail(ErrorCode::NetworkFailed, "The connection request to the Unreal Editor could not be sent.");
            openSent = true;
            nextOpen = now + kOpenEvery;
        }
        const int ready = waitReadable(listener, sliceUntil(std::min(deadline, nextOpen)));
        if (ready < 0) return fail(ErrorCode::NetworkFailed, "The Unreal command port stopped working.");
        if (ready == 0) continue;
        SOCKET accepted = accept(listener.get(), nullptr, nullptr);
        if (accepted == INVALID_SOCKET) continue;
        channel = Socket(accepted);
        return {};
    }
}

// Opens the command port, lets the editor connect, sends the command and reads the answer.
UnrealCommandResult connectAndRun(const Socket& udp, const sockaddr_in& group, const UnrealEditorNode& editor,
                                  std::string_view command, UnrealExecMode mode, std::chrono::seconds timeout,
                                  std::stop_token stop, bool& openSent, Socket& channel)
{
    UnrealCommandResult out;
    Socket listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = 0;
    address.sin_addr = localAddress();
    int length = sizeof(address);
    if (!listener.valid() || bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listener.get(), 1) != 0 ||
        getsockname(listener.get(), reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        out.status = fail(ErrorCode::NetworkFailed, "No command port could be opened on " +
                                                        std::string(kUnrealBindAddress) + ".");
        return out;
    }
    const Json openMessage = unrealMessage("open_connection", unrealSourceId(), editor.nodeId,
                                    Json{{"command_ip", std::string(kUnrealBindAddress)},
                                         {"command_port", static_cast<int>(ntohs(address.sin_port))}});
    Status accepted = acceptEditor(udp, group, listener, openMessage, editor, stop, openSent, channel);
    if (!accepted.ok()) {
        out.status = std::move(accepted);
        return out;
    }
    listener.reset();

    const DWORD sendLimit = static_cast<DWORD>(std::chrono::duration_cast<milliseconds>(timeout).count());
    setsockopt(channel.get(), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&sendLimit), sizeof(sendLimit));
    const Json message = unrealMessage("command", unrealSourceId(), editor.nodeId,
                                       Json{{"command", std::string(command)},
                                            {"unattended", true},
                                            {"exec_mode", std::string(unrealExecModeName(mode))}});
    if (!sendAll(channel, messageText(message))) {
        out.status = fail(ErrorCode::EngineFailed, "The command could not be sent to the Unreal Editor.");
        return out;
    }
    return readAnswer(channel, timeout, std::move(stop));
}

}

// Reads the endpoint from PLYNTH_UNREAL_MULTICAST, or gives the default when it is not set.
UnrealEndpointRead unrealEndpoint()
{
    UnrealEndpointRead out;
    const std::wstring name(kUnrealEndpointVariable.begin(), kUnrealEndpointVariable.end());
    std::array<wchar_t, 128> buffer{};
    const DWORD length = GetEnvironmentVariableW(name.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) return out;
    const auto refused = [&] {
        out.status = fail(ErrorCode::InvalidValue, std::string(kUnrealEndpointVariable) +
                                                       " must be a multicast group and port, such as 239.0.0.1:6866.");
        return out;
    };
    if (length >= buffer.size()) return refused();
    std::string text;
    for (DWORD i = 0; i < length; ++i) {
        if (buffer[i] > 0x7F) return refused();
        text += static_cast<char>(buffer[i]);
    }
    const std::size_t colon = text.rfind(':');
    if (colon == std::string::npos) return refused();
    UnrealEndpoint endpoint;
    endpoint.group = text.substr(0, colon);
    const std::string port = text.substr(colon + 1);
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (port.empty() || port.size() > 5 || !std::all_of(port.begin(), port.end(), digit)) return refused();
    endpoint.port = std::stoi(port);
    sockaddr_in ignored{};
    if (!checkEndpoint(endpoint, ignored).ok()) return refused();
    out.endpoint = std::move(endpoint);
    return out;
}

// Pings the group, collects every editor that answers until the wait is over, and gives each one once.
UnrealDiscovery findUnrealEditors(const UnrealEndpoint& endpoint, std::chrono::milliseconds wait,
                                  std::stop_token stop)
{
    UnrealDiscovery out;
    WinsockSession winsock;
    if (!winsock.ok) {
        out.status = fail(ErrorCode::NetworkFailed, "Windows sockets could not be started.");
        return out;
    }
    sockaddr_in group{};
    Socket udp;
    if (Status status = openGroupSocket(endpoint, group, udp); !status.ok()) {
        out.status = std::move(status);
        return out;
    }
    const Json ping = unrealMessage("ping", unrealSourceId());
    const auto deadline = Clock::now() + wait;
    auto nextPing = Clock::now();
    std::vector<char> buffer(kDatagramBytes);
    while (true) {
        if (stop.stop_requested()) {
            out.status = fail(ErrorCode::Cancelled, "Looking for Unreal editors was stopped.");
            return out;
        }
        const auto now = Clock::now();
        if (now >= nextPing) {
            if (!sendToGroup(udp, group, ping)) {
                out.status = fail(ErrorCode::NetworkFailed, "The Unreal discovery message could not be sent.");
                return out;
            }
            nextPing = now + kPingEvery;
        }
        if (now >= deadline) break;
        const int ready = waitReadable(udp, sliceUntil(std::min(deadline, nextPing)));
        if (ready < 0) {
            out.status = fail(ErrorCode::NetworkFailed, "The Unreal discovery socket stopped working.");
            return out;
        }
        if (ready == 0) continue;
        const int got = recv(udp.get(), buffer.data(), static_cast<int>(buffer.size()), 0);
        if (got <= 0) continue;
        const std::string_view bytes(buffer.data(), static_cast<std::size_t>(got));
        const UnrealMessageRead message = parseUnrealMessage(bytes);
        if (!message.status.ok() || message.type != "pong" || !forThisNode(message)) continue;
        UnrealNodeRead node = unrealNodeFromPong(message.source, message.data);
        if (!node.status.ok()) continue;
        const bool known = std::any_of(out.editors.begin(), out.editors.end(),
                                       [&](const UnrealEditorNode& seen) { return seen.nodeId == node.node.nodeId; });
        if (!known) out.editors.push_back(std::move(node.node));
    }
    return out;
}

// The name Unreal uses on the wire for an execution mode.
std::string_view unrealExecModeName(UnrealExecMode mode)
{
    switch (mode) {
    case UnrealExecMode::ExecuteFile: return "ExecuteFile";
    case UnrealExecMode::ExecuteStatement: return "ExecuteStatement";
    case UnrealExecMode::EvaluateStatement: return "EvaluateStatement";
    }
    return "ExecuteFile";
}

// Opens a command connection to one editor, runs one command, then closes the connection again.
UnrealCommandResult runUnrealCommand(const UnrealEndpoint& endpoint, const UnrealEditorNode& editor,
                                     std::string_view command, UnrealExecMode mode, std::chrono::seconds timeout,
                                     std::stop_token stop)
{
    UnrealCommandResult out;
    if (editor.nodeId.empty()) {
        out.status = fail(ErrorCode::InvalidValue, "No Unreal Editor was named for the command.");
        return out;
    }
    WinsockSession winsock;
    if (!winsock.ok) {
        out.status = fail(ErrorCode::NetworkFailed, "Windows sockets could not be started.");
        return out;
    }
    sockaddr_in group{};
    Socket udp;
    if (Status status = openGroupSocket(endpoint, group, udp); !status.ok()) {
        out.status = std::move(status);
        return out;
    }
    bool openSent = false;
    Socket channel;
    out = connectAndRun(udp, group, editor, command, mode, timeout, std::move(stop), openSent, channel);
    if (openSent) sendToGroup(udp, group, unrealMessage("close_connection", unrealSourceId(), editor.nodeId));
    channel.reset();
    return out;
}

// Builds one protocol message. dest and data are left out when empty, as Unreal's own client does.
Json unrealMessage(std::string_view type, std::string_view source, std::string_view dest, const Json& data)
{
    Json message = Json::object();
    message["version"] = kProtocolVersion;
    message["magic"] = std::string(kProtocolMagic);
    message["type"] = std::string(type);
    message["source"] = std::string(source);
    if (!dest.empty()) message["dest"] = std::string(dest);
    const bool empty = data.is_null() || ((data.is_object() || data.is_array() || data.is_string()) && data.empty());
    if (!empty) message["data"] = data;
    return message;
}

// Reads one protocol message and checks its version, magic and field types.
UnrealMessageRead parseUnrealMessage(std::string_view text)
{
    UnrealMessageRead out;
    const Json message = Json::parse(text.begin(), text.end(), nullptr, false);
    if (message.is_discarded() || !message.is_object()) {
        out.status = fail(ErrorCode::BadFile, "The Unreal message is not a JSON object.");
        return out;
    }
    const auto version = message.find("version");
    const bool versionOk = version != message.end() &&
                           ((version->is_number_integer() && version->get<long long>() == kProtocolVersion) ||
                            (version->is_number_float() && version->get<double>() == kProtocolVersion));
    if (!versionOk) {
        out.status = fail(ErrorCode::BadFile, "The Unreal message has another protocol version.", "/version");
        return out;
    }
    const auto magic = message.find("magic");
    if (magic == message.end() || !magic->is_string() || magic->get<std::string>() != kProtocolMagic) {
        out.status = fail(ErrorCode::BadFile, "The Unreal message has the wrong magic text.", "/magic");
        return out;
    }
    if (!readText(message, "type", true, out.type)) {
        out.status = fail(ErrorCode::BadFile, "The Unreal message has no type.", "/type");
        return out;
    }
    if (!readText(message, "source", true, out.source)) {
        out.status = fail(ErrorCode::BadFile, "The Unreal message has no source.", "/source");
        return out;
    }
    if (!readText(message, "dest", false, out.dest)) {
        out.status = fail(ErrorCode::BadFile, "The Unreal message's dest is not text.", "/dest");
        return out;
    }
    if (const auto data = message.find("data"); data != message.end()) out.data = *data;
    return out;
}

// Reads an editor's details from its pong.
UnrealNodeRead unrealNodeFromPong(std::string_view source, const Json& data)
{
    UnrealNodeRead out;
    if (source.empty()) {
        out.status = fail(ErrorCode::BadFile, "The pong has no node id.", "/source");
        return out;
    }
    if (!data.is_object()) {
        out.status = fail(ErrorCode::BadFile, "The pong has no editor details.", "/data");
        return out;
    }
    UnrealEditorNode node;
    node.nodeId = std::string(source);
    std::string engineRoot;
    std::string projectRoot;
    struct Field {
        const char* name;
        bool required;
        std::string* into;
    };
    const Field fields[] = {
        {"user", true, &node.user},
        {"machine", true, &node.machine},
        {"engine_version", true, &node.engineVersion},
        {"engine_root", true, &engineRoot},
        {"project_root", false, &projectRoot},
        {"project_name", false, &node.projectName},
    };
    for (const Field& field : fields) {
        if (!readText(data, field.name, field.required, *field.into)) {
            out.status = fail(ErrorCode::BadFile,
                              std::string("The pong's ") + field.name + " is missing or not text.",
                              std::string("/data/") + field.name);
            return out;
        }
    }
    node.engineRoot = pathFromUtf8(engineRoot);
    node.projectRoot = pathFromUtf8(projectRoot);
    out.node = std::move(node);
    return out;
}

// This process's node id, made once.
const std::string& unrealSourceId()
{
    static const std::string id = makeNodeId();
    return id;
}

}
