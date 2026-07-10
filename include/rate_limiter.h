//
// Created by daviddaeschler on 7/10/26.
//

#ifndef SLIM_GUESTBOOK_API_RATE_LIMITER_H
#define SLIM_GUESTBOOK_API_RATE_LIMITER_H

#include <unordered_map>
#include <chrono>
#include <string>
#include <mutex>

namespace sgb_api {
    class RateLimiter {
    public:
        RateLimiter() = default;
        ~RateLimiter() = default;

        RateLimiter(const RateLimiter&) = default;
        RateLimiter& operator=(const RateLimiter&) = default;
        RateLimiter(RateLimiter&&) noexcept = default;
        RateLimiter& operator=(RateLimiter&&) noexcept = default;

        bool isRateLimited(const std::string& host);
    private:
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> rateLimitMap;
        std::mutex rateLimitMapMutex;
    };
}

#endif //SLIM_GUESTBOOK_API_RATE_LIMITER_H
