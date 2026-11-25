#include "Http.h"

#include "UpdateInternals.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <mutex>
#include <optional>

namespace plynth::update_detail {

namespace {

std::atomic<long long> connections{0};

// UTF-8 to UTF-16.
std::wstring widen(std::string_view text)
{
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size > 0 ? size : 0), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

// UTF-16 to UTF-8.
std::string narrow(std::wstring_view text)
{
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

// Closes a WinHTTP handle when it goes out of scope.
struct Handle {
    HINTERNET value = nullptr;
    ~Handle()
    {
        if (value) WinHttpCloseHandle(value);
    }
};

// Where one request goes.
struct Target {
    bool secure = true;
    std::wstring host;
    INTERNET_PORT port = 0;
    std::wstring path;
};

// Takes an address apart and checks it against the rules: https (plain http only in a test environment), no
// user name or password, a host on the allow list. Empty with a reason when it breaks one.
std::optional<Target> checkAddress(const std::string& url, std::string& why)
{
    const std::wstring wide = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof parts;
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUserNameLength = static_cast<DWORD>(-1);
    parts.dwPasswordLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (wide.empty() || !WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts)) {
        why = "The address " + url + " is not valid.";
        return std::nullopt;
    }
    const UpdateEnvironment& env = environment();
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    if (!secure && !(parts.nScheme == INTERNET_SCHEME_HTTP && env.allowPlainHttp)) {
        why = "The address " + url + " does not use HTTPS.";
        return std::nullopt;
    }
    if (parts.dwUserNameLength > 0 || parts.dwPasswordLength > 0) {
        why = "The address " + url + " carries a user name.";
        return std::nullopt;
    }
    std::string host = parts.lpszHostName ? narrow(std::wstring_view(parts.lpszHostName, parts.dwHostNameLength)) : "";
    for (char& c : host) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    bool allowed = false;
    for (const std::string& name : env.allowedHosts) allowed = allowed || name == host;
    if (!allowed) {
        why = "The address " + url + " is not on a host Plynth updates from.";
        return std::nullopt;
    }
    Target target;
    target.secure = secure;
    target.host = widen(host);
    target.port = parts.nPort;
    if (parts.lpszUrlPath) target.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo) target.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (target.path.empty()) target.path = L"/";
    return target;
}

// Closes the request from another thread when the caller asks to stop, which ends a waiting WinHTTP call.
struct Canceller {
    std::mutex lock;
    HINTERNET request = nullptr;
    bool stopped = false;

    // Starts watching a request. False when a stop came first; the request is then closed here.
    bool watch(HINTERNET handle)
    {
        std::lock_guard guard(lock);
        if (stopped) {
            WinHttpCloseHandle(handle);
            return false;
        }
        request = handle;
        return true;
    }
    // Stops the request being watched.
    void stop()
    {
        std::lock_guard guard(lock);
        stopped = true;
        if (request) WinHttpCloseHandle(request);
        request = nullptr;
    }
    // Closes the watched request unless a stop already did.
    void release()
    {
        std::lock_guard guard(lock);
        if (request) WinHttpCloseHandle(request);
        request = nullptr;
    }
    // Says whether a stop came.
    bool wasStopped()
    {
        std::lock_guard guard(lock);
        return stopped;
    }
};

// The plain reason for a WinHTTP error.
Status networkError(DWORD error, const UpdateEnvironment& env)
{
    switch (error) {
    case ERROR_WINHTTP_TIMEOUT:
        return fail(ErrorCode::NetworkFailed, "The release server stopped answering for " +
                                                  std::to_string(env.stallLimit.count() / 1000) + " seconds.");
    case ERROR_WINHTTP_SECURE_FAILURE:
        return fail(ErrorCode::NetworkFailed, "The release server's certificate was refused.");
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return fail(ErrorCode::NetworkFailed, "The release server's name could not be found. Check the internet connection.");
    case ERROR_WINHTTP_CANNOT_CONNECT:
        return fail(ErrorCode::NetworkFailed, "The release server could not be reached. Check the internet connection.");
    default:
        return fail(ErrorCode::NetworkFailed, "The connection to the release server failed (error " + std::to_string(error) + ").");
    }
}

// Reads a number header of a request. Empty when it is not there.
std::optional<unsigned long long> numberHeader(HINTERNET request, DWORD which)
{
    ULONGLONG value = 0;
    DWORD size = sizeof value;
    if (!WinHttpQueryHeaders(request, which | WINHTTP_QUERY_FLAG_NUMBER64, WINHTTP_HEADER_NAME_BY_INDEX, &value, &size,
                             WINHTTP_NO_HEADER_INDEX)) {
        return std::nullopt;
    }
    return value;
}

// Reads the Location header of a redirect. Empty when there is none.
std::wstring locationHeader(HINTERNET request)
{
    DWORD size = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0 || size > 16 * 1024) return {};
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &size,
                             WINHTTP_NO_HEADER_INDEX)) {
        return {};
    }
    value.resize(size / sizeof(wchar_t));
    return value;
}

// Opens the WinHTTP session with the rules every request shares.
HINTERNET openSession(const UpdateEnvironment& env)
{
    const DWORD access = env.useSystemProxy ? WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY : WINHTTP_ACCESS_TYPE_NO_PROXY;
    HINTERNET session = WinHttpOpen(L"Plynth", access, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return nullptr;
    const int limit = static_cast<int>(env.stallLimit.count());
    WinHttpSetTimeouts(session, limit, limit, limit, limit);
    // The wait for the response headers has its own limit, 90 seconds unless set.
    DWORD responseLimit = static_cast<DWORD>(limit);
    WinHttpSetOption(session, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT, &responseLimit, sizeof responseLimit);
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof redirects);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
    }
#else
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
#endif
    return session;
}

// Turns off everything a request could add on its own: cookies, logins, redirects and kept connections.
void restrictRequest(HINTERNET request)
{
    DWORD features = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION |
                     WINHTTP_DISABLE_KEEP_ALIVE;
    WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &features, sizeof features);
    DWORD logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    WinHttpSetOption(request, WINHTTP_OPTION_AUTOLOGON_POLICY, &logon, sizeof logon);
}

// Makes an absolute address from a redirect's Location and the address it came from.
std::string resolveLocation(const std::wstring& location, const Target& from)
{
    if (location.rfind(L"/", 0) == 0 && location.rfind(L"//", 0) != 0) {
        return narrow((from.secure ? L"https://" : L"http://") + from.host + L":" + std::to_wstring(from.port) + location);
    }
    return narrow(location);
}

}

// The hosts the shipped program may reach.
const std::vector<std::string>& defaultAllowedHosts()
{
    static const std::vector<std::string> hosts{"api.github.com", "github.com", "objects.githubusercontent.com",
                                                "release-assets.githubusercontent.com"};
    return hosts;
}

// Requests made so far.
long long httpConnectionCount()
{
    return connections.load();
}

// Sends a GET, following at most kMaxRedirects redirects to allowed hosts, and streams the body to sink.
HttpResult httpGet(const HttpRequest& request, const HttpSink& sink, const std::function<void(long long)>& onLength,
                   std::stop_token stop)
{
    const UpdateEnvironment& env = environment();
    HttpResult result;
    Canceller canceller;
    std::stop_callback onStop(stop, [&canceller] { canceller.stop(); });

    std::string url = request.url;
    std::string why;
    auto target = checkAddress(url, why);
    if (!target) {
        result.status = fail(ErrorCode::NetworkFailed, why);
        return result;
    }
    Handle session{openSession(env)};
    if (!session.value) {
        result.status = networkError(GetLastError(), env);
        return result;
    }
    const std::wstring headers = L"Accept: " + widen(request.accept) + L"\r\n";

    for (int redirects = 0;; ++redirects) {
        if (canceller.wasStopped()) {
            result.status = fail(ErrorCode::Cancelled, "The update was stopped.");
            return result;
        }
        Handle connection{WinHttpConnect(session.value, target->host.c_str(), target->port, 0)};
        if (!connection.value) {
            result.status = networkError(GetLastError(), env);
            return result;
        }
        HINTERNET handle = WinHttpOpenRequest(connection.value, L"GET", target->path.c_str(), nullptr,
                                              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              target->secure ? WINHTTP_FLAG_SECURE : 0);
        if (!handle) {
            result.status = networkError(GetLastError(), env);
            return result;
        }
        restrictRequest(handle);
        if (!canceller.watch(handle)) {
            result.status = fail(ErrorCode::Cancelled, "The update was stopped.");
            return result;
        }
        struct Release {
            Canceller& c;
            ~Release() { c.release(); }
        } release{canceller};

        // Every failure below is a cancel when a stop came, since the stop closed the request.
        auto failed = [&](DWORD error) {
            result.status = canceller.wasStopped() ? fail(ErrorCode::Cancelled, "The update was stopped.")
                                                   : networkError(error, env);
            return result;
        };
        ++connections;
        if (!WinHttpSendRequest(handle, headers.c_str(), static_cast<DWORD>(headers.size()), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(handle, nullptr)) {
            return failed(GetLastError());
        }
        const auto code = [&]() -> int {
            DWORD value = 0;
            DWORD size = sizeof value;
            if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                     WINHTTP_HEADER_NAME_BY_INDEX, &value, &size, WINHTTP_NO_HEADER_INDEX)) {
                return 0;
            }
            return static_cast<int>(value);
        }();
        result.httpStatus = code;

        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            if (redirects >= kMaxRedirects) {
                result.status = fail(ErrorCode::NetworkFailed, "The release server redirected more than " +
                                                                   std::to_string(kMaxRedirects) + " times.");
                return result;
            }
            const std::wstring location = locationHeader(handle);
            if (location.empty()) {
                result.status = fail(ErrorCode::NetworkFailed, "The release server gave a redirect without an address.");
                return result;
            }
            url = resolveLocation(location, *target);
            target = checkAddress(url, why);
            if (!target) {
                result.status = fail(ErrorCode::NetworkFailed, why);
                return result;
            }
            continue;
        }
        if (code != 200) {
            result.status = fail(ErrorCode::NetworkFailed, "The release server answered " + std::to_string(code) + ".");
            return result;
        }

        const auto length = numberHeader(handle, WINHTTP_QUERY_CONTENT_LENGTH);
        if (length && *length > static_cast<unsigned long long>(request.maxBytes)) {
            result.status = fail(ErrorCode::NetworkFailed, "The answer of the release server is too big.");
            return result;
        }
        if (onLength) onLength(length ? static_cast<long long>(*length) : 0);

        std::string buffer(64 * 1024, '\0');
        for (;;) {
            DWORD read = 0;
            if (!WinHttpReadData(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
                return failed(GetLastError());
            }
            if (read == 0) break;
            result.received += read;
            if (result.received > request.maxBytes) {
                result.status = fail(ErrorCode::NetworkFailed, "The answer of the release server is too big.");
                return result;
            }
            if (Status s = sink(std::string_view(buffer.data(), read)); !s.ok()) {
                result.status = std::move(s);
                return result;
            }
            if (canceller.wasStopped()) {
                result.status = fail(ErrorCode::Cancelled, "The update was stopped.");
                return result;
            }
        }
        if (canceller.wasStopped()) {
            result.status = fail(ErrorCode::Cancelled, "The update was stopped.");
            return result;
        }
        if (length && static_cast<unsigned long long>(result.received) != *length) {
            result.status = fail(ErrorCode::NetworkFailed, "The download ended before it was complete.");
            return result;
        }
        return result;
    }
}

// Sends a GET and collects the body.
HttpResult httpGetText(const HttpRequest& request, std::string& body, std::stop_token stop)
{
    body.clear();
    return httpGet(request, [&body](std::string_view piece) {
        body.append(piece);
        return Status{};
    }, {}, stop);
}

}
