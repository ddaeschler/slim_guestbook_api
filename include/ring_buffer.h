//
// Created by daviddaeschler on 7/8/26.
//

#ifndef SLIM_GUESTBOOK_API_RING_BUFFER_H
#define SLIM_GUESTBOOK_API_RING_BUFFER_H

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>

namespace sgb_api::ring_buffer {
    constexpr std::uint32_t HANDLE_MAX_SIZE = 32;
    constexpr std::uint32_t MESSAGE_MAX_SIZE = 128;

    struct Header {
        char magic[8] = "GBK0001";
        std::uint32_t version = 1;
        std::uint32_t message_size = MESSAGE_MAX_SIZE;
        std::uint32_t handle_size = HANDLE_MAX_SIZE;
        std::uint32_t max_entries = 10'000; // 1.6 MB
        std::uint32_t count = 0;
        std::uint64_t sequence = 0;
    };

    struct Entry {
        char handle[HANDLE_MAX_SIZE];
        char message[MESSAGE_MAX_SIZE];
    };

    constexpr std::uint32_t HEADER_SIZE = sizeof(Header);
    constexpr std::uint32_t ENTRY_SIZE = sizeof(Entry);
    constexpr std::uint32_t PAGE_SIZE = 3;

    class RingBuffer {
    public:
        RingBuffer(std::filesystem::path parentPath);
        ~RingBuffer() = default;

        RingBuffer(RingBuffer&&) noexcept = default;
        RingBuffer& operator=(RingBuffer&&) noexcept = default;

        RingBuffer(const RingBuffer&) = delete;
        RingBuffer& operator=(const RingBuffer&) = delete;

        void open();
        void openExistingRingBuffer(const std::filesystem::path& ringBufferPath);
        void createNewRingBufferFile(const std::filesystem::path& ringBufferPath);
        void writeNext(const std::string& handle, const std::string& message);
        std::vector<Entry> readPage(std::uint32_t pageNumber);

    private:
        std::filesystem::path _parentPath;
        std::fstream _ringBufferFile;
        Header _header;
    };

}

#endif //SLIM_GUESTBOOK_API_RING_BUFFER_H
