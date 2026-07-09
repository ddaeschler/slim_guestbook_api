//
// Created by daviddaeschler on 7/8/26.
//

#include <drogon/drogon.h>
#include <filesystem>
#include <functional>
#include <trantor/utils/ConcurrentTaskQueue.h>
#include <fstream>

#include "ring_buffer.h"

std::fstream ringBufferFile;
sgb_api::ring_buffer::Header header;

void getPageHandler(const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback) {


}

void checkCreateRingBuffer() {
    std::filesystem::path ringBufferPath = drogon::app().getDocumentRoot() / std::filesystem::path("ring_buffer.bin");

    if (std::filesystem::exists(ringBufferPath)) {
        // open the file and read the header
        if (std::filesystem::file_size(ringBufferPath) < sgb_api::ring_buffer::HEADER_SIZE) {
            throw std::runtime_error("Ring buffer file size is too small. Expected at least "
                + std::to_string( sgb_api::ring_buffer::HEADER_SIZE) + " bytes");
        }


        LOG_INFO << "Ring buffer file already exists";
        return;
    }

    // if the file doesn't exist, create it and write the header
    ringBufferFile.open(ringBufferPath, std::ios::out | std::ios::binary);
    if (!ringBufferFile.is_open()) {
        throw std::runtime_error("Failed to create ring buffer file");
    }

    ringBufferFile.write(reinterpret_cast<char *>(&header), sgb_api::ring_buffer::HEADER_SIZE);
    LOG_INFO << "Ring buffer file created with version " << header.version;
}

int main(int argc, char *argv[]) {
    LOG_DEBUG << "Current working directory: " << std::filesystem::current_path();

    drogon::app().registerHandler("/read", &getPageHandler, {drogon::Get});
    drogon::app().loadConfigFile("config.json");

    try {
        checkCreateRingBuffer();
    } catch (const std::exception &e) {
        LOG_ERROR << "Error creating ring buffer: " << e.what();
        return 1;
    }

    drogon::app().run();
    return 0;
}