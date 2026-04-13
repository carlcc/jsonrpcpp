#pragma once

#include <jsonrpc/jsonrpc.hpp>
#include <iostream>
#include <string>
#include <atomic>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <vector>

// ================================================================
//  Shared test types
// ================================================================

struct Point {
    double x, y;
};

inline void to_json(nlohmann::json& j, const Point& p) {
    j = {{"x", p.x}, {"y", p.y}};
}

inline void from_json(const nlohmann::json& j, Point& p) {
    j.at("x").get_to(p.x);
    j.at("y").get_to(p.y);
}

// ================================================================
//  Test result tracker
// ================================================================

struct TestResults {
    int pass = 0;
    int fail = 0;

    void report(const std::string& suite_name) const {
        std::cout << "  [" << suite_name << "] "
                  << pass << " passed, "
                  << fail << " failed" << std::endl;
    }
};

// ================================================================
//  Test helper: send and verify (synchronous wait for async results)
// ================================================================

// Send request and verify response contains expected substring
inline void send_expect(jsonrpc::Server& server, TestResults& r,
                        const std::string& request,
                        const std::string& expected_substr,
                        const std::string& test_name) {
    std::cout << "  [TEST] " << test_name << std::endl;
    std::cout << "  >>> " << request << std::endl;

    // Use mutex+cv to synchronously wait for async callback
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    std::string captured_response;

    server.process(request, [&](const std::string& response) {
        std::lock_guard<std::mutex> lock(mtx);
        captured_response = response;
        done = true;
        cv.notify_one();
    });

    // Wait up to 5 seconds for the response
    {
        std::unique_lock<std::mutex> lock(mtx);
        if (!cv.wait_for(lock, std::chrono::seconds(5), [&] { return done; })) {
            std::cout << "  ❌ FAIL — timeout waiting for response" << std::endl;
            r.fail++;
            std::cout << std::endl;
            return;
        }
    }

    std::cout << "  <<< " << (captured_response.empty() ? "(no response - notification)" : captured_response)
              << std::endl;

    if (captured_response.find(expected_substr) != std::string::npos) {
        std::cout << "  ✅ PASS" << std::endl;
        r.pass++;
    } else {
        std::cout << "  ❌ FAIL — expected to contain: " << expected_substr << std::endl;
        r.fail++;
    }
    std::cout << std::endl;
}

// Send request and verify response is empty (notification)
inline void send_expect_empty(jsonrpc::Server& server, TestResults& r,
                              const std::string& request,
                              const std::string& test_name) {
    std::cout << "  [TEST] " << test_name << std::endl;
    std::cout << "  >>> " << request << std::endl;

    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    std::string captured_response;

    server.process(request, [&](const std::string& response) {
        std::lock_guard<std::mutex> lock(mtx);
        captured_response = response;
        done = true;
        cv.notify_one();
    });

    {
        std::unique_lock<std::mutex> lock(mtx);
        if (!cv.wait_for(lock, std::chrono::seconds(5), [&] { return done; })) {
            std::cout << "  ❌ FAIL — timeout waiting for response" << std::endl;
            r.fail++;
            std::cout << std::endl;
            return;
        }
    }

    if (captured_response.empty()) {
        std::cout << "  <<< (no response - notification)" << std::endl;
        std::cout << "  ✅ PASS" << std::endl;
        r.pass++;
    } else {
        std::cout << "  <<< " << captured_response << std::endl;
        std::cout << "  ❌ FAIL — expected empty response (notification)" << std::endl;
        r.fail++;
    }
    std::cout << std::endl;
}

// ================================================================
//  Test suite interface
// ================================================================

using TestSuiteFn = std::function<TestResults()>;
