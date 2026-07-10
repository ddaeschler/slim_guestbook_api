#ifndef SLIM_GUESTBOOK_API_TEST_FRAMEWORK_H
#define SLIM_GUESTBOOK_API_TEST_FRAMEWORK_H

#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace test_framework {

    struct TestCase {
        std::string name;
        std::function<void()> run;
    };

    inline std::vector<TestCase>& registry() {
        static std::vector<TestCase> tests;
        return tests;
    }

    struct Registrar {
        Registrar(std::string name, std::function<void()> run) {
            registry().push_back({std::move(name), std::move(run)});
        }
    };

    inline void require(bool condition, std::string_view expression, std::string_view file, int line) {
        if (!condition) {
            throw std::runtime_error(std::string(file) + ":" + std::to_string(line)
                + ": requirement failed: " + std::string(expression));
        }
    }

    inline int runAll() {
        int failures = 0;
        for (const auto& test : registry()) {
            try {
                test.run();
                std::cout << "[PASS] " << test.name << '\n';
            } catch (const std::exception& e) {
                ++failures;
                std::cerr << "[FAIL] " << test.name << ": " << e.what() << '\n';
            } catch (...) {
                ++failures;
                std::cerr << "[FAIL] " << test.name << ": unknown exception\n";
            }
        }

        std::cout << registry().size() - failures << " passed, " << failures << " failed\n";
        return failures == 0 ? 0 : 1;
    }

}

#define TF_CONCAT_DETAIL(a, b) a##b
#define TF_CONCAT(a, b) TF_CONCAT_DETAIL(a, b)

#define TEST_CASE(name) \
    static void TF_CONCAT(test_case_, __LINE__)(); \
    static test_framework::Registrar TF_CONCAT(test_registrar_, __LINE__)(name, TF_CONCAT(test_case_, __LINE__)); \
    static void TF_CONCAT(test_case_, __LINE__)()

#define REQUIRE(expression) \
    test_framework::require(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

#define REQUIRE_THROWS(expression) \
    do { \
        bool tf_threw = false; \
        try { \
            (void)(expression); \
        } catch (...) { \
            tf_threw = true; \
        } \
        test_framework::require(tf_threw, #expression " throws", __FILE__, __LINE__); \
    } while (false)

#endif
