//
// Created by daviddaeschler on 7/8/26.
//

#include <drogon/drogon.h>
#include <filesystem>
#include <functional>
#include <trantor/utils/ConcurrentTaskQueue.h>
#include <fstream>

#include "ring_buffer.h"

std::optional<sgb_api::ring_buffer::RingBuffer> activeBuffer;

void getPageHandler(const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback) {

}

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