#include "test_framework.h"

#include "ring_buffer.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace {

    using sgb_api::ring_buffer::Entry;
    using sgb_api::ring_buffer::RingBuffer;

    struct TempDirectory {
        std::filesystem::path path;

        TempDirectory() {
            const auto base = std::filesystem::temp_directory_path();
            for (int i = 0; i < 100; ++i) {
                auto candidate = base / ("slim_guestbook_ring_buffer_test_"
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

    std::string handleOf(const Entry& entry) {
        return entry.handle;
    }

    std::string messageOf(const Entry& entry) {
        return entry.message;
    }

    RingBuffer openBuffer(const std::filesystem::path& path) {
        RingBuffer buffer(path);
        buffer.open();
        return buffer;
    }

    void writeEntry(RingBuffer& buffer, int index) {
        buffer.writeNext("h" + std::to_string(index), "m" + std::to_string(index));
    }

    void requireMessages(const std::vector<Entry>& entries, const std::vector<std::string>& messages) {
        REQUIRE(entries.size() == messages.size());
        for (std::size_t i = 0; i < messages.size(); ++i) {
            REQUIRE(messageOf(entries[i]) == messages[i]);
        }
    }

}

TEST_CASE("empty page zero returns no entries") {
    TempDirectory temp;
    auto buffer = openBuffer(temp.path);

    const auto page = buffer.readPage(0);

    REQUIRE(page.empty());
}

TEST_CASE("empty nonzero page throws") {
    TempDirectory temp;
    auto buffer = openBuffer(temp.path);

    REQUIRE_THROWS(buffer.readPage(1));
}

TEST_CASE("first page returns newest entries first") {
    TempDirectory temp;
    auto buffer = openBuffer(temp.path);

    for (int i = 0; i < 5; ++i) {
        writeEntry(buffer, i);
    }

    requireMessages(buffer.readPage(0), {"m4", "m3", "m2"});
}

TEST_CASE("partial older page returns only retained entries on that page") {
    TempDirectory temp;
    auto buffer = openBuffer(temp.path);

    for (int i = 0; i < 5; ++i) {
        writeEntry(buffer, i);
    }

    requireMessages(buffer.readPage(1), {"m1", "m0"});
    REQUIRE_THROWS(buffer.readPage(2));
}

TEST_CASE("exact page boundary rejects one past the last page") {
    TempDirectory temp;
    auto buffer = openBuffer(temp.path);

    for (int i = 0; i < 6; ++i) {
        writeEntry(buffer, i);
    }

    requireMessages(buffer.readPage(0), {"m5", "m4", "m3"});
    requireMessages(buffer.readPage(1), {"m2", "m1", "m0"});
    REQUIRE_THROWS(buffer.readPage(2));
}

TEST_CASE("wrapped buffer returns newest and oldest retained pages") {
    TempDirectory temp;
    auto buffer = openBuffer(temp.path);

    for (int i = 0; i < 10002; ++i) {
        writeEntry(buffer, i);
    }

    requireMessages(buffer.readPage(0), {"m10001", "m10000", "m9999"});
    requireMessages(buffer.readPage(3332), {"m5", "m4", "m3"});
    requireMessages(buffer.readPage(3333), {"m2"});
    REQUIRE_THROWS(buffer.readPage(3334));
}

TEST_CASE("entries persist after reopening the buffer") {
    TempDirectory temp;
    {
        auto buffer = openBuffer(temp.path);
        for (int i = 0; i < 4; ++i) {
            writeEntry(buffer, i);
        }
    }

    auto reopened = openBuffer(temp.path);

    requireMessages(reopened.readPage(0), {"m3", "m2", "m1"});
    requireMessages(reopened.readPage(1), {"m0"});
}

int main() {
    return test_framework::runAll();
}
