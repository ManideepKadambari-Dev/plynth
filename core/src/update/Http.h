#pragma once

#include "plynth/core/Status.h"

#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

// HTTPS for the updater, through Windows' WinHTTP, so core needs no TLS library.
namespace plynth::update_detail {

// One GET request.
struct HttpRequest {
    std::string url;
    std::string accept;
    long long maxBytes = 0;
};

// The answer of a request. httpStatus is the last status the server gave, 0 when none came.
struct HttpResult {
    Status status;
    int httpStatus = 0;
    long long received = 0;
};

// The most redirects followed.
inline constexpr int kMaxRedirects = 5;

// Receives the body in pieces. A failed status ends the request with that status.
using HttpSink = std::function<Status(std::string_view piece)>;

// Sends a GET and passes the body to sink. onLength, when given, gets the length the server announced before
// the first piece. Refuses an address that is not https, has a user name, or names a host off the allow list,
// for the first request and every redirect. NetworkFailed for every failure of the network or the server, a
// stall, a body that is short or too big, or a status other than 200. Cancelled when stopped.
HttpResult httpGet(const HttpRequest& request, const HttpSink& sink,
                   const std::function<void(long long length)>& onLength = {}, std::stop_token stop = {});

// The same, with the body collected into text.
HttpResult httpGetText(const HttpRequest& request, std::string& body, std::stop_token stop = {});

// How many connections this process opened for the updater, so a test can see that none was made.
long long httpConnectionCount();

// The hosts the shipped program may reach.
const std::vector<std::string>& defaultAllowedHosts();

}
