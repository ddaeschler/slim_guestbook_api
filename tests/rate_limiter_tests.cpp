#include "test_framework.h"

#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>

#define private public
#include "rate_limiter.h"
#undef private

namespace {
    constexpr auto RATE_LIMIT_WINDOW = std::chrono::seconds(1);

    void waitPastRateLimitWindow() {
        std::this_thread::sleep_for(RATE_LIMIT_WINDOW + std::chrono::milliseconds(100));
    }
}

TEST_CASE("first call for a host is not rate limited") {
    sgb_api::RateLimiter limiter;

    REQUIRE(!limiter.isRateLimited("203.0.113.10"));
}

TEST_CASE("second call for a host inside one second is rate limited") {
    sgb_api::RateLimiter limiter;

    REQUIRE(!limiter.isRateLimited("203.0.113.10"));
    REQUIRE(limiter.isRateLimited("203.0.113.10"));
}

TEST_CASE("calls are tracked independently per host") {
    sgb_api::RateLimiter limiter;

    REQUIRE(!limiter.isRateLimited("203.0.113.10"));
    REQUIRE(!limiter.isRateLimited("203.0.113.11"));
    REQUIRE(limiter.isRateLimited("203.0.113.10"));
    REQUIRE(limiter.isRateLimited("203.0.113.11"));
}

TEST_CASE("host is allowed again after one second") {
    sgb_api::RateLimiter limiter;

    REQUIRE(!limiter.isRateLimited("203.0.113.10"));
    waitPastRateLimitWindow();
    REQUIRE(!limiter.isRateLimited("203.0.113.10"));
}

TEST_CASE("isRateLimited removes stale entries before adding a new host") {
    sgb_api::RateLimiter limiter;

    REQUIRE(!limiter.isRateLimited("203.0.113.10"));
    REQUIRE(!limiter.isRateLimited("203.0.113.11"));
    REQUIRE(limiter.rateLimitMap.size() == 2);

    waitPastRateLimitWindow();

    REQUIRE(!limiter.isRateLimited("203.0.113.12"));
    REQUIRE(limiter.rateLimitMap.size() == 1);
    REQUIRE(limiter.rateLimitMap.contains("203.0.113.12"));
}

int main() {
    return test_framework::runAll();
}
