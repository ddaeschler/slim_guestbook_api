//
// Created by daviddaeschler on 7/8/26.
//

#include <drogon/drogon.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <trantor/utils/ConcurrentTaskQueue.h>
#include <fstream>
#include <mutex>

#include "rate_limiter.h"
#include "ring_buffer.h"

trantor::ConcurrentTaskQueue dbTaskQueue(1, "DBTasks");
std::optional<sgb_api::ring_buffer::RingBuffer> activeBuffer;
std::mutex activeBufferMutex;
std::vector<std::string> trustedHosts = {"127.0.0.1", "::1"};
std::set<std::string> corsAllowedHosts;

namespace {

    constexpr auto CORS_ALLOWED_HOST_KEY = "cors_allowed_host";
    constexpr auto CORS_ALLOWED_HOSTS_KEY = "cors_allowed_hosts";
    constexpr auto ACCESS_CONTROL_ALLOW_ORIGIN = "Access-Control-Allow-Origin";
    constexpr auto ACCESS_CONTROL_ALLOW_METHODS = "Access-Control-Allow-Methods";
    constexpr auto ACCESS_CONTROL_ALLOW_HEADERS = "Access-Control-Allow-Headers";
    constexpr auto ACCESS_CONTROL_MAX_AGE = "Access-Control-Max-Age";
    constexpr auto VARY = "Vary";

    std::string trimWhitespace(std::string value) {
        const auto first = std::ranges::find_if(value, [](const unsigned char c) {
            return !std::isspace(c);
        });
        value.erase(value.begin(), first);

        const auto last = std::ranges::find_if(value.rbegin(), value.rend(), [](const unsigned char c) {
            return !std::isspace(c);
        });
        value.erase(last.base(), value.end());

        return value;
    }

    void addCorsAllowedHost(const std::string& host) {
        const auto trimmed = trimWhitespace(host);
        if (!trimmed.empty()) {
            corsAllowedHosts.insert(trimmed);
        }
    }

    void addCorsAllowedHosts(const Json::Value& configuredHosts) {
        if (configuredHosts.isString()) {
            addCorsAllowedHost(configuredHosts.asString());
            return;
        }

        if (!configuredHosts.isArray()) {
            return;
        }

        for (const auto& host : configuredHosts) {
            if (host.isString()) {
                addCorsAllowedHost(host.asString());
            }
        }
    }

    std::string originHost(const std::string& origin) {
        auto hostStart = origin.find("://");
        hostStart = hostStart == std::string::npos ? 0 : hostStart + 3;

        auto hostEnd = origin.find('/', hostStart);
        auto host = origin.substr(hostStart, hostEnd == std::string::npos ? std::string::npos : hostEnd - hostStart);

        if (!host.empty() && host.front() == '[') {
            const auto ipv6End = host.find(']');
            return ipv6End == std::string::npos ? host : host.substr(1, ipv6End - 1);
        }

        const auto portStart = host.find(':');
        return portStart == std::string::npos ? host : host.substr(0, portStart);
    }

    bool isCorsOriginAllowed(const std::string& origin) {
        if (origin.empty()) {
            return false;
        }

        if (corsAllowedHosts.contains("*") || corsAllowedHosts.contains(origin)) {
            return true;
        }

        const auto host = originHost(origin);
        return corsAllowedHosts.contains(host);
    }

}

void loadCorsAllowedHostsFromCustomConfig(const Json::Value& customConfig) {
    corsAllowedHosts.clear();
    addCorsAllowedHosts(customConfig[CORS_ALLOWED_HOST_KEY]);
    addCorsAllowedHosts(customConfig[CORS_ALLOWED_HOSTS_KEY]);
}

void applyCorsHeaders(const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& response) {
    const auto origin = req->getHeader("Origin");
    if (!isCorsOriginAllowed(origin)) {
        return;
    }

    response->addHeader(ACCESS_CONTROL_ALLOW_ORIGIN,
        corsAllowedHosts.contains("*") ? std::string("*") : origin);
    response->addHeader(ACCESS_CONTROL_ALLOW_METHODS, "GET, POST, OPTIONS");
    response->addHeader(ACCESS_CONTROL_ALLOW_HEADERS, "Content-Type");
    response->addHeader(ACCESS_CONTROL_MAX_AGE, "86400");
    response->addHeader(VARY, "Origin");
}

drogon::HttpResponsePtr buildErrorResponse(drogon::HttpStatusCode statusCode, const std::string& body) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(statusCode);
    response->setBody(body);
    return response;
}

void dispatchResponse(std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const drogon::HttpResponsePtr& response) {
    if (drogon::app().getLoop()->isRunning()) {
        drogon::app().getLoop()->runInLoop([callback = std::move(callback), response]() {
            callback(response);
        });
        return;
    }

    callback(response);
}

void dispatchResponse(const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const drogon::HttpResponsePtr& response) {
    applyCorsHeaders(req, response);
    dispatchResponse(std::move(callback), response);
}

void optionsHandler(const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr &)>&& callback) {
    const auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::HttpStatusCode::k204NoContent);
    applyCorsHeaders(req, response);
    callback(response);
}

void getPageHandler(const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr &)>&& callback) {

    auto pageNumberParm = req->getParameter("pageNumber");
    if (pageNumberParm.empty()) {
        const auto response = buildErrorResponse(drogon::HttpStatusCode::k400BadRequest, "invalid page number.");
        applyCorsHeaders(req, response);
        callback(response);
        return;
    }

    std::uint32_t pageNumber;
    try {
        pageNumber = static_cast<std::uint32_t>(std::stoi(pageNumberParm));
    } catch (const std::exception&) {
        const auto response = buildErrorResponse(drogon::HttpStatusCode::k400BadRequest, "invalid page number.");
        applyCorsHeaders(req, response);
        callback(response);
        return;
    }

    dbTaskQueue.runTaskInQueue([callback = std::move(callback), req, pageNumber]() mutable {
        std::lock_guard lock(activeBufferMutex);

        try {
            auto page = activeBuffer->readPage(pageNumber);
            Json::Value pageJson;
            pageJson["pageNumber"] = pageNumber;
            pageJson["entries"] = Json::Value(Json::arrayValue);

            for (const auto& [handle, message] : page) {
                Json::Value entryJson;
                entryJson["handle"] = handle;
                entryJson["message"] = message;
                pageJson["entries"].append(entryJson);
            }

            const auto response = drogon::HttpResponse::newHttpJsonResponse(pageJson);
            response->setStatusCode(drogon::HttpStatusCode::k200OK);

            dispatchResponse(req, std::move(callback), response);

        } catch (const std::range_error&) {
            const auto response = buildErrorResponse(drogon::HttpStatusCode::k400BadRequest,
                "invalid page number.");
            dispatchResponse(req, std::move(callback), response);

        } catch (const std::exception& e) {
            const auto response = buildErrorResponse(drogon::HttpStatusCode::k500InternalServerError,
                "internal server error.");
            dispatchResponse(req, std::move(callback), response);
        }
    });
}

std::string trim(std::string value) {
    const auto first = std::ranges::find_if(value, [](const unsigned char c) {
        return !std::isspace(c);
    });
    value.erase(value.begin(), first);

    const auto last = std::ranges::find_if(value.rbegin(), value.rend(), [](const unsigned char c) {
        return !std::isspace(c);
    });
    value.erase(last.base(), value.end());

    return value;
}

std::string firstForwardedHost(const std::string& xff) {
    const auto comma = xff.find(',');
    return trim(xff.substr(0, comma));
}

std::string extractPosterHostAddress(const drogon::HttpRequestPtr& req) {
    const std::string xff = req->getHeader("X-Forwarded-For");
    std::string reqHost = req->peerAddr().toIp();

    if (!xff.empty() && std::ranges::find(trustedHosts, reqHost) != trustedHosts.end()) {
        reqHost = firstForwardedHost(xff);
    }

    return reqHost;
}

sgb_api::RateLimiter rateLimiter;
void postHandler(const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr &)>&& callback) {

    // first check rate-limiting information for this IP address,
    // we accept at most one request per second
    const std::string reqHost = extractPosterHostAddress(req);
    if (rateLimiter.isRateLimited(reqHost)) {
        const auto response = buildErrorResponse(drogon::HttpStatusCode::k429TooManyRequests, "too many requests.");
        applyCorsHeaders(req, response);
        callback(response);
        return;
    }

    // next validate the payload
    const auto body = req->jsonObject();
    if (!body) {
        const auto response = buildErrorResponse(drogon::HttpStatusCode::k400BadRequest, "invalid json.");
        applyCorsHeaders(req, response);
        callback(response);
        return;
    }

    if (!body->isObject() ||
        !body->isMember("handle") ||
        !body->isMember("message") ||
        !(*body)["handle"].isString() ||
        !(*body)["message"].isString()) {

        const auto response = buildErrorResponse(drogon::HttpStatusCode::k400BadRequest, "invalid json.");
        applyCorsHeaders(req, response);
        callback(response);
        return;
    }

    const auto handle = body->get("handle", "").asString();
    const auto message = body->get("message", "").asString();

    if (handle.size() >= sgb_api::ring_buffer::HANDLE_MAX_SIZE ||
        message.size() >= sgb_api::ring_buffer::MESSAGE_MAX_SIZE) {

        const auto response = buildErrorResponse(drogon::HttpStatusCode::k400BadRequest, "handle or message too large.");
        applyCorsHeaders(req, response);
        callback(response);
        return;
    }

    // validation passed, write the data
    dbTaskQueue.runTaskInQueue([callback = std::move(callback), req, handle, message]() mutable {
        std::lock_guard lock(activeBufferMutex);

        try {
            activeBuffer->writeNext(handle, message);

            const auto response = drogon::HttpResponse::newHttpResponse();
            dispatchResponse(req, std::move(callback), response);

        } catch (const std::range_error&) {
            const auto response = buildErrorResponse(drogon::HttpStatusCode::k400BadRequest,
                "invalid page number.");
            dispatchResponse(req, std::move(callback), response);

        } catch (const std::exception& e) {
            const auto response = buildErrorResponse(drogon::HttpStatusCode::k500InternalServerError,
                "internal server error.");
            dispatchResponse(req, std::move(callback), response);
        }
    });
}

#ifndef SLIM_GUESTBOOK_API_DISABLE_MAIN
int main(int argc, char *argv[]) {
    LOG_DEBUG << "Current working directory: " << std::filesystem::current_path();

    drogon::app().registerHandler("/read", &getPageHandler, {drogon::Get});
    drogon::app().registerHandler("/write", &postHandler, {drogon::Post});
    drogon::app().registerHandler("/read", &optionsHandler, {drogon::Options});
    drogon::app().registerHandler("/write", &optionsHandler, {drogon::Options});
    drogon::app().loadConfigFile("config.json");

    //load up trusted hosts
    const auto cfgTrusted = drogon::app().getCustomConfig()["trusted_hosts"];
    if (cfgTrusted && cfgTrusted.isArray()) {
        for (const auto& trustedHost : cfgTrusted) {
            if (trustedHost.isString()) {
                trustedHosts.push_back(trustedHost.asString());
            }
        }
    }
    loadCorsAllowedHostsFromCustomConfig(drogon::app().getCustomConfig());

    sgb_api::ring_buffer::RingBuffer ringBuffer(drogon::app().getDocumentRoot());
    try {
        ringBuffer.open();
    } catch (const std::exception &e) {
        LOG_ERROR << "Error opening ring buffer: " << e.what();
        return 1;
    }

    activeBuffer.emplace(std::move(ringBuffer));

    drogon::app().registerBeginningAdvice([] {
        const auto listeners = drogon::app().getListeners();

        for (const auto& addr : listeners) {
            LOG_INFO << "Listening on "
                     << addr.toIp()
                     << ':'
                     << addr.toPort();
        }
    });

    drogon::app().run();
    return 0;
}
#endif
