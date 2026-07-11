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
    /// Maximum persisted handle length, including the null terminator.
    constexpr std::uint32_t HANDLE_MAX_SIZE = 32;

    /// Maximum persisted message length, including the null terminator.
    constexpr std::uint32_t MESSAGE_MAX_SIZE = 128;

    /**
     * @brief On-disk metadata stored at the beginning of the ring buffer file.
     *
     * The header describes the fixed entry layout and tracks the current write
     * sequence. Values are validated when an existing buffer is opened.
     */
    struct Header {
        char magic[8] = "GBK0001";
        std::uint32_t version = 1;
        std::uint32_t message_size = MESSAGE_MAX_SIZE;
        std::uint32_t handle_size = HANDLE_MAX_SIZE;
        std::uint32_t max_entries = 10'000; // 1.6 MB
        std::uint32_t count = 0;
        std::uint64_t sequence = 0;
    };

    /**
     * @brief Fixed-size guestbook record stored in the ring buffer.
     *
     * Strings are persisted as null-terminated character arrays and are
     * truncated to fit the fixed field sizes.
     */
    struct Entry {
        char handle[HANDLE_MAX_SIZE];
        char message[MESSAGE_MAX_SIZE];
    };

    /// Number of bytes occupied by the on-disk header.
    constexpr std::uint32_t HEADER_SIZE = sizeof(Header);

    /// Number of bytes occupied by each on-disk entry.
    constexpr std::uint32_t ENTRY_SIZE = sizeof(Entry);

    /// Number of entries returned by a full read page.
    constexpr std::uint32_t PAGE_SIZE = 3;

    /**
     * @brief Persistent fixed-size ring buffer for guestbook entries.
     *
     * The buffer stores entries in `ring_buffer.bin` under the configured parent
     * path. New writes overwrite the oldest retained entries after capacity is
     * reached, while reads return pages ordered newest-to-oldest.
     */
    class RingBuffer {
    public:
        /**
         * @brief Construct a ring buffer rooted at the given parent directory.
         *
         * Call open() before reading or writing entries.
         *
         * @param parentPath Directory containing the `ring_buffer.bin` file.
         */
        RingBuffer(std::filesystem::path parentPath);
        ~RingBuffer() = default;

        RingBuffer(RingBuffer&&) noexcept = default;
        RingBuffer& operator=(RingBuffer&&) noexcept = default;

        RingBuffer(const RingBuffer&) = delete;
        RingBuffer& operator=(const RingBuffer&) = delete;

        /**
         * @brief Open the existing buffer file or create a new one.
         *
         * @throws std::runtime_error if the existing file is invalid or cannot be opened.
         */
        void open();

        /**
         * @brief Open the existing buffer file at an exact path or create a new one.
         *
         * @param ringBufferPath Path to the ring buffer file.
         * @throws std::runtime_error if the existing file is invalid or cannot be opened.
         */
        void openFile(const std::filesystem::path& ringBufferPath);

        /**
         * @brief Open and validate an existing ring buffer file.
         *
         * @param ringBufferPath Path to the buffer file.
         * @throws std::runtime_error if the file is missing, malformed, corrupt, or inaccessible.
         */
        void openExistingRingBuffer(const std::filesystem::path& ringBufferPath);

        /**
         * @brief Create a new ring buffer file and write the initial header.
         *
         * @param ringBufferPath Path where the buffer file should be created.
         * @throws std::runtime_error if the file cannot be created or reopened.
         */
        void createNewRingBufferFile(const std::filesystem::path& ringBufferPath);

        /**
         * @brief Append the next guestbook entry.
         *
         * The handle and message are truncated to the fixed field sizes. Once the
         * buffer reaches capacity, this overwrites the oldest retained entry.
         *
         * @param handle Guest handle to store.
         * @param message Guest message to store.
         */
        void writeNext(const std::string& handle, const std::string& message);

        /**
         * @brief Read one page of entries, newest first.
         *
         * Page 0 contains the newest entries. Older pages contain earlier retained
         * entries and the final page may contain fewer than PAGE_SIZE entries.
         *
         * @param pageNumber Zero-based page number.
         * @return Entries for the requested page, ordered newest-to-oldest.
         * @throws std::runtime_error if pageNumber is outside the retained range.
         */
        std::vector<Entry> readPage(std::uint32_t pageNumber);

        /**
         * @brief Remove the newest retained guestbook entry.
         *
         * This only updates the persisted header counters; the entry bytes remain
         * in the backing file but are no longer returned by readPage().
         *
         * The buffer must contain at least one entry before this is called.
         */
        void popLast();

    private:
        std::filesystem::path _parentPath;
        std::fstream _ringBufferFile;
        Header _header;
    };

}

#endif //SLIM_GUESTBOOK_API_RING_BUFFER_H
