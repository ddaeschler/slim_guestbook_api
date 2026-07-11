#include "ring_buffer.h"

#include <trantor/utils/Logger.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

    using sgb_api::ring_buffer::Entry;
    using sgb_api::ring_buffer::HANDLE_MAX_SIZE;
    using sgb_api::ring_buffer::MESSAGE_MAX_SIZE;
    using sgb_api::ring_buffer::RingBuffer;

    void printUsage(std::ostream& out) {
        out << "Usage:\n"
            << "  ring_buffer_cli <ring-buffer-file> read <page-number>\n"
            << "  ring_buffer_cli <ring-buffer-file> write <handle> <message>\n"
            << "  ring_buffer_cli <ring-buffer-file> popLast\n";
    }

    std::uint32_t parsePageNumber(const std::string& value) {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(value, &consumed);
        if (consumed != value.size() || parsed > std::numeric_limits<std::uint32_t>::max()) {
            throw std::invalid_argument("invalid page number");
        }

        return static_cast<std::uint32_t>(parsed);
    }

    std::string boundedString(const char* data, std::size_t maxSize) {
        const auto end = std::find(data, data + maxSize, '\0');
        return {data, static_cast<std::size_t>(end - data)};
    }

    std::string joinMessage(int argc, char* argv[], int startIndex) {
        std::string message;
        for (int i = startIndex; i < argc; ++i) {
            if (!message.empty()) {
                message += ' ';
            }
            message += argv[i];
        }

        return message;
    }

    RingBuffer openRingBuffer(const std::filesystem::path& ringBufferPath) {
        RingBuffer buffer(ringBufferPath.parent_path());
        buffer.openFile(ringBufferPath);
        return buffer;
    }

    int readPage(const std::filesystem::path& ringBufferPath, const std::string& pageNumberArgument) {
        auto buffer = openRingBuffer(ringBufferPath);
        const auto pageNumber = parsePageNumber(pageNumberArgument);
        const auto entries = buffer.readPage(pageNumber);

        if (entries.empty()) {
            std::cout << "No entries.\n";
            return 0;
        }

        for (const Entry& entry : entries) {
            std::cout << boundedString(entry.handle, HANDLE_MAX_SIZE)
                << '\t'
                << boundedString(entry.message, MESSAGE_MAX_SIZE)
                << '\n';
        }

        return 0;
    }

    int writeEntry(const std::filesystem::path& ringBufferPath, std::string_view handle, std::string message) {
        auto buffer = openRingBuffer(ringBufferPath);
        buffer.writeNext(std::string(handle), message);
        std::cout << "Wrote entry.\n";
        return 0;
    }

    int popLastEntry(const std::filesystem::path& ringBufferPath) {
        auto buffer = openRingBuffer(ringBufferPath);
        buffer.popLast();
        std::cout << "Removed newest entry.\n";
        return 0;
    }

}

int main(int argc, char* argv[]) {
    trantor::Logger::setLogLevel(trantor::Logger::kWarn);

    if (argc < 3) {
        printUsage(std::cerr);
        return 2;
    }

    const std::filesystem::path ringBufferPath = argv[1];
    const std::string command = argv[2];

    try {
        if (command == "read") {
            if (argc != 4) {
                printUsage(std::cerr);
                return 2;
            }

            return readPage(ringBufferPath, argv[3]);
        }

        if (command == "write") {
            if (argc < 5) {
                printUsage(std::cerr);
                return 2;
            }

            return writeEntry(ringBufferPath, argv[3], joinMessage(argc, argv, 4));
        }

        if (command == "popLast") {
            if (argc != 3) {
                printUsage(std::cerr);
                return 2;
            }

            return popLastEntry(ringBufferPath);
        }

        printUsage(std::cerr);
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
