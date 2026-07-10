#include "test_framework.h"

#include "ring_buffer.h"

#include <drogon/drogon.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

extern std::optional<sgb_api::ring_buffer::RingBuffer> activeBuffer;
extern std::mutex activeBufferMutex;

void getPageHandler(const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback);

namespace {

    using sgb_api::ring_buffer::RingBuffer;

    struct TempDirectory {
        std::filesystem::path path;

        TempDirectory() {
            const auto base = std::filesystem::temp_directory_path();
            for (int i = 0; i < 100; ++i) {
                auto candidate = base / ("slim_guestbook_get_page_handler_test_"
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

    void openActiveBuffer(const std::filesystem::path& path) {
        std::lock_guard lock(activeBufferMutex);
        activeBuffer.reset();
        activeBuffer.emplace(path);
        activeBuffer->open();
    }

    void resetActiveBuffer() {
        std::lock_guard lock(activeBufferMutex);
        activeBuffer.reset();
    }

    drogon::HttpResponsePtr invokeGetPageHandler(const std::optional<std::string>& pageNumber) {
        auto req = drogon::HttpRequest::newHttpRequest();
        if (pageNumber.has_value()) {
            req->setParameter("pageNumber", *pageNumber);
        }

        std::promise<drogon::HttpResponsePtr> responsePromise;
        auto responseFuture = responsePromise.get_future();

        getPageHandler(req, [&responsePromise](const drogon::HttpResponsePtr& response) {
            responsePromise.set_value(response);
        });

        const auto status = responseFuture.wait_for(std::chrono::seconds(2));
        REQUIRE(status == std::future_status::ready);
        return responseFuture.get();
    }

    void writeEntry(const std::string& handle, const std::string& message) {
        std::lock_guard lock(activeBufferMutex);
        activeBuffer->writeNext(handle, message);
    }

}

TEST_CASE("getPageHandler rejects missing page number") {
    const auto response = invokeGetPageHandler(std::nullopt);

    REQUIRE(response->getStatusCode() == drogon::HttpStatusCode::k400BadRequest);
    REQUIRE(response->getBody() == "invalid page number.");
}

TEST_CASE("getPageHandler rejects non-numeric page number") {
    const auto response = invokeGetPageHandler("abc");

    REQUIRE(response->getStatusCode() == drogon::HttpStatusCode::k400BadRequest);
    REQUIRE(response->getBody() == "invalid page number.");
}

TEST_CASE("getPageHandler returns page entries as JSON") {
    TempDirectory temp;
    openActiveBuffer(temp.path);

    writeEntry("h0", "m0");
    writeEntry("h1", "m1");
    writeEntry("h2", "m2");
    writeEntry("h3", "m3");

    const auto response = invokeGetPageHandler("0");

    REQUIRE(response->getStatusCode() == drogon::HttpStatusCode::k200OK);
    const auto json = response->getJsonObject();
    REQUIRE(json != nullptr);
    REQUIRE((*json)["pageNumber"].asUInt() == 0);
    REQUIRE((*json)["entries"].size() == 3);
    REQUIRE((*json)["entries"][0]["handle"].asString() == "h3");
    REQUIRE((*json)["entries"][0]["message"].asString() == "m3");
    REQUIRE((*json)["entries"][1]["handle"].asString() == "h2");
    REQUIRE((*json)["entries"][1]["message"].asString() == "m2");
    REQUIRE((*json)["entries"][2]["handle"].asString() == "h1");
    REQUIRE((*json)["entries"][2]["message"].asString() == "m1");

    resetActiveBuffer();
}

TEST_CASE("getPageHandler maps out-of-range pages to bad request") {
    TempDirectory temp;
    openActiveBuffer(temp.path);
    writeEntry("h0", "m0");

    const auto response = invokeGetPageHandler("1");

    REQUIRE(response->getStatusCode() == drogon::HttpStatusCode::k400BadRequest);
    REQUIRE(response->getBody() == "invalid page number.");

    resetActiveBuffer();
}

int main() {
    return test_framework::runAll();
}
