//
// Created by daviddaeschler on 7/8/26.
//

#include <drogon/drogon.h>
#include <filesystem>
#include <functional>
#include <trantor/utils/ConcurrentTaskQueue.h>
#include <fstream>
#include <mutex>

#include "ring_buffer.h"

trantor::ConcurrentTaskQueue dbTaskQueue(1, "DBTasks");
std::optional<sgb_api::ring_buffer::RingBuffer> activeBuffer;
std::mutex activeBufferMutex;

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

void getPageHandler(const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr &)>&& callback) {

    auto pageNumberParm = req->getParameter("pageNumber");
    if (pageNumberParm.empty()) {
        const auto response = buildErrorResponse(drogon::HttpStatusCode::k400BadRequest, "invalid page number.");
        callback(response);
        return;
    }

    std::uint32_t pageNumber;
    try {
        pageNumber = static_cast<std::uint32_t>(std::stoi(pageNumberParm));
    } catch (const std::exception&) {
        const auto response = buildErrorResponse(drogon::HttpStatusCode::k400BadRequest, "invalid page number.");
        callback(response);
        return;
    }

    dbTaskQueue.runTaskInQueue([callback = std::move(callback), pageNumber]() mutable {
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

            dispatchResponse(std::move(callback), response);

        } catch (const std::range_error&) {
            const auto response = buildErrorResponse(drogon::HttpStatusCode::k400BadRequest,
                "invalid page number.");
            dispatchResponse(std::move(callback), response);

        } catch (const std::exception& e) {
            const auto response = buildErrorResponse(drogon::HttpStatusCode::k500InternalServerError,
                "internal server error.");
            dispatchResponse(std::move(callback), response);
        }
    });
}

#ifndef SLIM_GUESTBOOK_API_DISABLE_MAIN
int main(int argc, char *argv[]) {
    LOG_DEBUG << "Current working directory: " << std::filesystem::current_path();

    drogon::app().registerHandler("/read", &getPageHandler, {drogon::Get});
    drogon::app().loadConfigFile("config.json");

    sgb_api::ring_buffer::RingBuffer ringBuffer(drogon::app().getDocumentRoot());
    try {
        ringBuffer.open();
    } catch (const std::exception &e) {
        LOG_ERROR << "Error opening ring buffer: " << e.what();
        return 1;
    }

    activeBuffer.emplace(std::move(ringBuffer));

    drogon::app().run();
    return 0;
}
#endif
