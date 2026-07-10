#include "rate_limiter.h"

#include <chrono>

namespace sgb_api {
    constexpr auto RATE_LIMIT_INTERVAL = std::chrono::seconds(1);

    bool RateLimiter::isRateLimited(const std::string& host) {
        const auto now = std::chrono::steady_clock::now();

        std::lock_guard lock(rateLimitMapMutex);
        for (auto it = rateLimitMap.begin(); it != rateLimitMap.end();) {
            if (now - it->second >= RATE_LIMIT_INTERVAL) {
                it = rateLimitMap.erase(it);
                continue;
            }

            ++it;
        }

        if (rateLimitMap.contains(host)) {
            return true;
        }

        rateLimitMap.emplace(host, now);
        return false;
    }
}