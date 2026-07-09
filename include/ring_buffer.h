//
// Created by daviddaeschler on 7/8/26.
//

#ifndef SLIM_GUESTBOOK_API_RING_BUFFER_H
#define SLIM_GUESTBOOK_API_RING_BUFFER_H

#include <cstdint>

namespace sgb_api::ring_buffer {
    struct Header {
        char magic[8] = "GBK0001";
        std::uint32_t version = 1;
        std::uint32_t message_size = 128;
        std::uint32_t handle_size = 32;
        std::uint64_t max_entries = message_size + handle_size * 10'000; // 1.6 MB
        std::uint64_t next_index = 0;
        std::uint64_t count = 0;
        std::uint64_t sequence = 0;
    };

    constexpr std::uint32_t HEADER_SIZE = sizeof(Header);
}


#endif //SLIM_GUESTBOOK_API_RING_BUFFER_H
