#include "test_framework.h"

#include "rate_limiter.h"
#include "ring_buffer.h"

#include <drogon/drogon.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

extern std::optional<sgb_api::ring_buffer::RingBuffer> activeBuffer;
extern std::mutex activeBufferMutex;
extern sgb_api::RateLimiter rateLimiter;

void postHandler(const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback);

namespace {

    using sgb_api::ring_buffer::RingBuffer;

    void resetRateLimiter() {
        rateLimiter.~RateLimiter();
        new (&rateLimiter) sgb_api::RateLimiter();
    }

    struct TempDirectory {
        std::filesystem::path path;

        TempDirectory() {
            const auto base = std::filesystem::temp_directory_path();
            for (int i = 0; i < 100; ++i) {
                auto candidate = base / ("slim_guestbook_post_handler_test_"
                    + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                    + "_" + std::to_string(i));
                if (std::filesystem::create_directory(candidate)) {
                    path = std::move(candidate);
                    return;
                }
            }

            throw std::runtime_error("failed to create temporary test directory");
        }

        ~TempDirectory() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }

        TempDirectory(const TempDirectory&) = delete;
        TempDirectory& operator=(const TempDirectory&) = delete;
    };

    struct HandlerFixture {
        TempDirectory temp;

        HandlerFixture() {
            resetRateLimiter();
            std::lock_guard lock(activeBufferMutex);
            activeBuffer.reset();
            activeBuffer.emplace(temp.path);
            activeBuffer->open();
        }

        ~HandlerFixture() {
            std::lock_guard lock(activeBufferMutex);
            activeBuffer.reset();
        }
    };

    drogon::HttpRequestPtr jsonRequest(const Json::Value& body) {
        return drogon::HttpRequest::newHttpJsonRequest(body);
    }

    drogon::HttpRequestPtr rawJsonRequest(std::string body) {
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setContentTypeCode(drogon::ContentType::CT_APPLICATION_JSON);
        req->setBody(std::move(body));
        return req;
    }

    Json::Value validBody(const std::string& handle = "alice", const std::string& message = "hello") {
        Json::Value body;
        body["handle"] = handle;
        body["message"] = message;
        return body;
    }

    drogon::HttpResponsePtr invokePostHandler(const drogon::HttpRequestPtr& req) {
        std::promise<drogon::HttpResponsePtr> responsePromise;
        auto responseFuture = responsePromise.get_future();

        postHandler(req, [&responsePromise](const drogon::HttpResponsePtr& response) {
            responsePromise.set_value(response);
        });

        const auto status = responseFuture.wait_for(std::chrono::seconds(2));
        REQUIRE(status == std::future_status::ready);
        return responseFuture.get();
    }

    std::vector<sgb_api::ring_buffer::Entry> readFirstPage() {
        std::lock_guard lock(activeBufferMutex);
        return activeBuffer->readPage(0);
    }

}

TEST_CASE("postHandler rejects malformed JSON") {
    HandlerFixture fixture;

    const auto response = invokePostHandler(rawJsonRequest("{"));

    REQUIRE(response->getStatusCode() == drogon::HttpStatusCode::k400BadRequest);
    REQUIRE(response->getBody() == "invalid json.");
    REQUIRE(readFirstPage().empty());
}

TEST_CASE("postHandler rejects missing fields") {
    HandlerFixture fixture;

    Json::Value body;
    body["handle"] = "alice";

    const auto response = invokePostHandler(jsonRequest(body));

    REQUIRE(response->getStatusCode() == drogon::HttpStatusCode::k400BadRequest);
    REQUIRE(response->getBody() == "invalid json.");
    REQUIRE(readFirstPage().empty());
}

TEST_CASE("postHandler rejects non-string fields") {
    HandlerFixture fixture;

    Json::Value body;
    body["handle"] = "alice";
    body["message"] = 42;

    const auto response = invokePostHandler(jsonRequest(body));

    REQUIRE(response->getStatusCode() == drogon::HttpStatusCode::k400BadRequest);
    REQUIRE(response->getBody() == "invalid json.");
    REQUIRE(readFirstPage().empty());
}

TEST_CASE("postHandler rejects oversized handle or message") {
    HandlerFixture fixture;

    const std::string oversizedHandle(sgb_api::ring_buffer::HANDLE_MAX_SIZE - 1, 'h');

    const auto response = invokePostHandler(jsonRequest(validBody(oversizedHandle, "hello")));

    REQUIRE(response->getStatusCode() == drogon::HttpStatusCode::k400BadRequest);
    REQUIRE(response->getBody() == "handle or message too large.");
    REQUIRE(readFirstPage().empty());
}

TEST_CASE("postHandler writes valid entries") {
    HandlerFixture fixture;

    const auto response = invokePostHandler(jsonRequest(validBody("alice", "hello")));

    REQUIRE(response->getStatusCode() == drogon::HttpStatusCode::k200OK);

    const auto page = readFirstPage();
    REQUIRE(page.size() == 1);
    REQUIRE(std::string(page[0].handle) == "alice");
    REQUIRE(std::string(page[0].message) == "hello");
}

TEST_CASE("postHandler rate limits repeated posts from the same host") {
    HandlerFixture fixture;

    const auto first = invokePostHandler(jsonRequest(validBody("alice", "first")));
    const auto second = invokePostHandler(jsonRequest(validBody("alice", "second")));

    REQUIRE(first->getStatusCode() == drogon::HttpStatusCode::k200OK);
    REQUIRE(second->getStatusCode() == drogon::HttpStatusCode::k429TooManyRequests);
    REQUIRE(second->getBody() == "too many requests.");

    const auto page = readFirstPage();
    REQUIRE(page.size() == 1);
    REQUIRE(std::string(page[0].message) == "first");
}

int main() {
    return test_framework::runAll();
}
