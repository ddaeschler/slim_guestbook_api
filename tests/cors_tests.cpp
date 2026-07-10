#include "test_framework.h"

#include <drogon/drogon.h>

#include <functional>
#include <future>
#include <set>
#include <string>

extern std::set<std::string> corsAllowedHosts;

void loadCorsAllowedHostsFromCustomConfig(const Json::Value& customConfig);
void getPageHandler(const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback);
void optionsHandler(const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback);

namespace {

    drogon::HttpRequestPtr requestWithOrigin(const std::string& origin) {
        auto req = drogon::HttpRequest::newHttpRequest();
        req->addHeader("Origin", origin);
        return req;
    }

    drogon::HttpResponsePtr invokeGetPageHandler(const drogon::HttpRequestPtr& req) {
        std::promise<drogon::HttpResponsePtr> responsePromise;
        auto responseFuture = responsePromise.get_future();

        getPageHandler(req, [&responsePromise](const drogon::HttpResponsePtr& response) {
            responsePromise.set_value(response);
        });

        const auto status = responseFuture.wait_for(std::chrono::seconds(2));
        REQUIRE(status == std::future_status::ready);
        return responseFuture.get();
    }

    drogon::HttpResponsePtr invokeOptionsHandler(const drogon::HttpRequestPtr& req) {
        std::promise<drogon::HttpResponsePtr> responsePromise;
        auto responseFuture = responsePromise.get_future();

        optionsHandler(req, [&responsePromise](const drogon::HttpResponsePtr& response) {
            responsePromise.set_value(response);
        });

        const auto status = responseFuture.wait_for(std::chrono::seconds(2));
        REQUIRE(status == std::future_status::ready);
        return responseFuture.get();
    }

}

TEST_CASE("loadCorsAllowedHostsFromCustomConfig accepts a single host") {
    Json::Value customConfig;
    customConfig["cors_allowed_host"] = "https://guestbook.example";

    loadCorsAllowedHostsFromCustomConfig(customConfig);

    REQUIRE(corsAllowedHosts.size() == 1);
    REQUIRE(corsAllowedHosts.contains("https://guestbook.example"));
}

TEST_CASE("loadCorsAllowedHostsFromCustomConfig accepts a list of hosts") {
    Json::Value customConfig;
    customConfig["cors_allowed_hosts"].append("https://guestbook.example");
    customConfig["cors_allowed_hosts"].append("https://admin.example");

    loadCorsAllowedHostsFromCustomConfig(customConfig);

    REQUIRE(corsAllowedHosts.size() == 2);
    REQUIRE(corsAllowedHosts.contains("https://guestbook.example"));
    REQUIRE(corsAllowedHosts.contains("https://admin.example"));
}

TEST_CASE("getPageHandler adds CORS headers for an allowed origin") {
    Json::Value customConfig;
    customConfig["cors_allowed_hosts"].append("https://guestbook.example");
    loadCorsAllowedHostsFromCustomConfig(customConfig);

    const auto response = invokeGetPageHandler(requestWithOrigin("https://guestbook.example"));

    REQUIRE(response->getStatusCode() == drogon::HttpStatusCode::k400BadRequest);
    REQUIRE(response->getHeader("Access-Control-Allow-Origin") == "https://guestbook.example");
    REQUIRE(response->getHeader("Access-Control-Allow-Methods") == "GET, POST, OPTIONS");
    REQUIRE(response->getHeader("Access-Control-Allow-Headers") == "Content-Type");
    REQUIRE(response->getHeader("Vary") == "Origin");
}

TEST_CASE("getPageHandler does not add CORS headers for a disallowed origin") {
    Json::Value customConfig;
    customConfig["cors_allowed_hosts"].append("https://guestbook.example");
    loadCorsAllowedHostsFromCustomConfig(customConfig);

    const auto response = invokeGetPageHandler(requestWithOrigin("https://attacker.example"));

    REQUIRE(response->getStatusCode() == drogon::HttpStatusCode::k400BadRequest);
    REQUIRE(response->getHeader("Access-Control-Allow-Origin").empty());
}

TEST_CASE("optionsHandler responds to CORS preflight for an allowed origin") {
    Json::Value customConfig;
    customConfig["cors_allowed_hosts"].append("guestbook.example");
    loadCorsAllowedHostsFromCustomConfig(customConfig);

    auto req = requestWithOrigin("https://guestbook.example");
    req->setMethod(drogon::HttpMethod::Options);
    req->addHeader("Access-Control-Request-Method", "POST");

    const auto response = invokeOptionsHandler(req);

    REQUIRE(response->getStatusCode() == drogon::HttpStatusCode::k204NoContent);
    REQUIRE(response->getHeader("Access-Control-Allow-Origin") == "https://guestbook.example");
    REQUIRE(response->getHeader("Access-Control-Max-Age") == "86400");
}

int main() {
    return test_framework::runAll();
}
