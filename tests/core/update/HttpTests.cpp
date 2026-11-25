#include "UpdateTestSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <cctype>
#include <cstring>
#include <set>

using namespace plynth;
using namespace plynth::test::update;
using namespace plynth::update_detail;

namespace {

// A GET of a path of the server, with the text collected.
HttpResult get(const ReleaseServer& server, const std::string& path, std::string& body, long long maxBytes = 1024 * 1024,
               std::stop_token stop = {})
{
    return httpGetText({server.base() + path, "application/octet-stream", maxBytes}, body, stop);
}

// The value of a request header, ignoring the case of its name. Empty when it is not there.
std::string header(const httplib::Headers& headers, const std::string& name)
{
    for (const auto& [key, value] : headers) {
        if (_stricmp(key.c_str(), name.c_str()) == 0) return value;
    }
    return {};
}

}

TEST_CASE("a request sends only what HTTPS needs, Accept and User-Agent", "[update][http]") {
    plynth::test::TempDir tmp("update-http-headers");
    ReleaseServer server;
    const ScopedUpdateEnvironment env(testEnvironment(server, {}, tmp.path()));
    server.setList("[]");
    std::string body;
    const HttpResult got = httpGetText({releasesListUrl(), "application/vnd.github+json", 1024}, body);
    plynth::test::requireOk(got.status);
    CHECK(body == "[]");
    const auto seen = server.seen();
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].path == "/api/repos/ManideepKadambari-Dev/plynth/releases");
    CHECK(header(seen[0].headers, "Accept") == "application/vnd.github+json");
    CHECK(header(seen[0].headers, "User-Agent") == "Plynth");
    const std::set<std::string> allowed{"accept", "user-agent", "host", "connection", "remote_addr", "remote_port",
                                        "local_addr", "local_port"};
    for (const auto& [key, value] : seen[0].headers) {
        std::string lower = key;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        INFO(key << ": " << value);
        CHECK(allowed.count(lower) == 1);
    }
}

TEST_CASE("an error status, a short body and a big body fail the request", "[update][http]") {
    plynth::test::TempDir tmp("update-http-errors");
    ReleaseServer server;
    const ScopedUpdateEnvironment env(testEnvironment(server, {}, tmp.path()));
    server.setCustom([](const httplib::Request& request, httplib::Response& response) {
        if (request.path == "/403") response.status = 403;
        else if (request.path == "/500") response.status = 500;
        else if (request.path == "/short") {
            response.set_content_provider(100, "application/octet-stream",
                                          [](std::size_t, std::size_t, httplib::DataSink& sink) {
                                              sink.write(std::string(50, 'a').data(), 50);
                                              return false;
                                          });
        } else if (request.path == "/big") {
            response.set_content(std::string(5000, 'b'), "application/octet-stream");
        } else if (request.path == "/big-chunked") {
            response.set_chunked_content_provider("application/octet-stream", [](std::size_t offset, httplib::DataSink& sink) {
                if (offset >= 5000) {
                    sink.done();
                    return true;
                }
                const std::string piece(500, 'c');
                sink.write(piece.data(), piece.size());
                return true;
            });
        } else {
            return false;
        }
        return true;
    });
    std::string body;
    HttpResult got = get(server, "/missing", body);
    CHECK(got.status.code == ErrorCode::NetworkFailed);
    CHECK(got.httpStatus == 404);
    got = get(server, "/403", body);
    CHECK(got.status.code == ErrorCode::NetworkFailed);
    CHECK(got.httpStatus == 403);
    got = get(server, "/500", body);
    CHECK(got.status.code == ErrorCode::NetworkFailed);
    CHECK(got.httpStatus == 500);
    got = get(server, "/short", body);
    CHECK(got.status.code == ErrorCode::NetworkFailed);
    got = get(server, "/big", body, 1000);
    CHECK(got.status.code == ErrorCode::NetworkFailed);
    got = get(server, "/big-chunked", body, 1000);
    CHECK(got.status.code == ErrorCode::NetworkFailed);
    got = get(server, "/big", body, 5000);
    CHECK(got.status.ok());
}

TEST_CASE("a stalled answer fails after the stall limit", "[update][http]") {
    plynth::test::TempDir tmp("update-http-stall");
    ReleaseServer server;
    const ScopedUpdateEnvironment env(testEnvironment(server, {}, tmp.path()));
    server.setCustom([](const httplib::Request&, httplib::Response& response) {
        response.set_content_provider(100, "application/octet-stream", [](std::size_t offset, std::size_t, httplib::DataSink& sink) {
            if (offset == 0) {
                sink.write("0123456789", 10);
                return true;
            }
            std::this_thread::sleep_for(std::chrono::seconds(6));
            return false;
        });
        return true;
    });
    std::string body;
    const auto started = std::chrono::steady_clock::now();
    const HttpResult got = get(server, "/stall", body);
    CHECK(got.status.code == ErrorCode::NetworkFailed);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(5));
}

TEST_CASE("a stop ends a request, also while it waits for data", "[update][http]") {
    plynth::test::TempDir tmp("update-http-cancel");
    ReleaseServer server;
    const ScopedUpdateEnvironment env(testEnvironment(server, {}, tmp.path()));
    server.setCustom([](const httplib::Request& request, httplib::Response& response) {
        const bool stall = request.path == "/stall";
        response.set_content_provider(100000, "application/octet-stream",
                                      [stall](std::size_t offset, std::size_t, httplib::DataSink& sink) {
                                          if (offset > 0 && stall) {
                                              std::this_thread::sleep_for(std::chrono::seconds(3));
                                              return false;
                                          }
                                          const std::string piece(1000, 'x');
                                          sink.write(piece.data(), piece.size());
                                          std::this_thread::sleep_for(std::chrono::milliseconds(20));
                                          return true;
                                      });
        return true;
    });

    std::stop_source fromSink;
    const HttpResult first = httpGet({server.base() + "/slow", "application/octet-stream", 1000000},
                                     [&](std::string_view) {
                                         fromSink.request_stop();
                                         return Status{};
                                     },
                                     {}, fromSink.get_token());
    CHECK(first.status.code == ErrorCode::Cancelled);

    std::stop_source fromThread;
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        fromThread.request_stop();
    });
    const auto started = std::chrono::steady_clock::now();
    std::string body;
    const HttpResult second = get(server, "/stall", body, 1000000, fromThread.get_token());
    stopper.join();
    CHECK(second.status.code == ErrorCode::Cancelled);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(1900));

    std::stop_source before;
    before.request_stop();
    const int seenBefore = static_cast<int>(server.seen().size());
    CHECK(get(server, "/slow", body, 1000000, before.get_token()).status.code == ErrorCode::Cancelled);
    CHECK(static_cast<int>(server.seen().size()) == seenBefore);
}

TEST_CASE("redirects are followed at most five times and only to allowed hosts", "[update][http]") {
    plynth::test::TempDir tmp("update-http-redirect");
    ReleaseServer server;
    const ScopedUpdateEnvironment env(testEnvironment(server, {}, tmp.path()));
    const std::string base = server.base();
    const std::string other = "http://localhost:" + std::to_string(server.port());
    server.setCustom([base, other](const httplib::Request& request, httplib::Response& response) {
        if (request.path.rfind("/r/", 0) == 0) {
            const int left = std::stoi(request.path.substr(3));
            if (left == 0) response.set_content("end", "text/plain");
            else response.set_redirect(base + "/r/" + std::to_string(left - 1), 302);
            return true;
        }
        if (request.path == "/relative") {
            response.set_redirect("/r/0", 301);
            return true;
        }
        if (request.path == "/away") {
            response.set_redirect(other + "/landed", 307);
            return true;
        }
        if (request.path == "/to-https") {
            response.set_redirect("https://example.com/landed", 302);
            return true;
        }
        if (request.path == "/cookie") {
            response.set_header("Set-Cookie", "session=secret; Path=/");
            response.set_redirect(base + "/r/0", 302);
            return true;
        }
        return false;
    });
    std::string body;
    HttpResult got = get(server, "/r/5", body);
    plynth::test::requireOk(got.status);
    CHECK(body == "end");
    got = get(server, "/r/6", body);
    CHECK(got.status.code == ErrorCode::NetworkFailed);
    CHECK(server.count("/r/0") == 1);
    got = get(server, "/relative", body);
    plynth::test::requireOk(got.status);
    CHECK(body == "end");
    got = get(server, "/away", body);
    CHECK(got.status.code == ErrorCode::NetworkFailed);
    CHECK(server.count("/landed") == 0);
    got = get(server, "/to-https", body);
    CHECK(got.status.code == ErrorCode::NetworkFailed);

    const std::size_t before = server.seen().size();
    got = get(server, "/cookie", body);
    plynth::test::requireOk(got.status);
    const auto seen = server.seen();
    REQUIRE(seen.size() == before + 2);
    for (std::size_t i = before; i < seen.size(); ++i) {
        CHECK(header(seen[i].headers, "Cookie").empty());
        CHECK(header(seen[i].headers, "Authorization").empty());
    }
}

TEST_CASE("the shipped rules refuse plain http and every host off the list before connecting", "[update][http]") {
    ReleaseServer server;
    const long long connections = httpConnectionCount();
    std::string body;
    for (const std::string& url : {server.base() + "/api/x", std::string("http://github.com/x"),
                                   std::string("https://example.com/x"), std::string("https://user:pw@github.com/x"),
                                   std::string("https://api.github.com.example.com/x"), std::string("ftp://github.com/x"),
                                   std::string("not an address")}) {
        INFO(url);
        const HttpResult got = httpGetText({url, "application/octet-stream", 1024}, body);
        CHECK(got.status.code == ErrorCode::NetworkFailed);
        CHECK(got.httpStatus == 0);
    }
    CHECK(httpConnectionCount() == connections);
    CHECK(server.seen().empty());
    CHECK(defaultAllowedHosts() == std::vector<std::string>{"api.github.com", "github.com", "objects.githubusercontent.com",
                                                            "release-assets.githubusercontent.com"});
}

TEST_CASE("the test rules still refuse a host off their list", "[update][http]") {
    plynth::test::TempDir tmp("update-http-list");
    ReleaseServer server;
    const ScopedUpdateEnvironment env(testEnvironment(server, {}, tmp.path()));
    std::string body;
    const long long connections = httpConnectionCount();
    const HttpResult got = httpGetText({"http://localhost:" + std::to_string(server.port()) + "/x", "text/plain", 1024}, body);
    CHECK(got.status.code == ErrorCode::NetworkFailed);
    CHECK(httpConnectionCount() == connections);
    CHECK(server.seen().empty());
}
