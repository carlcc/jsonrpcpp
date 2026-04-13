#include "test_helper.hpp"
#include <thread>
#include <chrono>
#include <future>

// ================================================================
//  Test: Async handlers with positional (array) params
//  Includes tests with real async work on separate threads
// ================================================================

TestResults test_async_array() {
    std::cout << "======================================" << std::endl;
    std::cout << " Async + Array Params" << std::endl;
    std::cout << "======================================" << std::endl << std::endl;

    TestResults r;
    jsonrpc::Server server;

    // --- Register handlers (immediate callback) ---

    server.bind_async("add",
        [](int a, int b, jsonrpc::ResponseCallback cb) {
            cb(a + b);
        });

    server.bind_async("greet",
        [](std::string name, jsonrpc::ResponseCallback cb) {
            cb("Hello, " + name + "!");
        });

    server.bind_async("distance",
        [](Point p1, Point p2, jsonrpc::ResponseCallback cb) {
            double dx = p1.x - p2.x;
            double dy = p1.y - p2.y;
            cb(std::sqrt(dx * dx + dy * dy));
        });

    server.bind_async("ping",
        [](jsonrpc::ResponseCallback cb) {
            cb("pong");
        });

    server.bind_async("void_func",
        [](jsonrpc::ResponseCallback cb) {
            cb(nullptr);
        });

    server.bind_async("empty_string",
        [](jsonrpc::ResponseCallback cb) {
            cb("");
        });

    server.bind_async("null_func",
        [](jsonrpc::ResponseCallback cb) {
            cb(nullptr);
        });

    server.bind_async("notify_log",
        [](std::string msg, jsonrpc::ResponseCallback cb) {
            std::cout << "  [ASYNC LOG] " << msg << std::endl;
            cb(nullptr);
        });

    server.bind_async("throw_error",
        [](int code, jsonrpc::ResponseCallback /*cb*/) {
            throw jsonrpc::JsonRpcError(code, "async array error");
        });

    // --- Register handlers (REAL ASYNC: callback from another thread) ---

    // Simulate slow computation on a worker thread
    server.bind_async("slow_add",
        [](int a, int b, jsonrpc::ResponseCallback cb) {
            std::thread([a, b, cb = std::move(cb)]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                cb(a + b);
            }).detach();
        });

    // Simulate async I/O returning a string
    server.bind_async("slow_greet",
        [](std::string name, jsonrpc::ResponseCallback cb) {
            std::thread([name = std::move(name), cb = std::move(cb)]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                cb("Async Hello, " + name + "!");
            }).detach();
        });

    // Simulate async computation with user-defined types
    server.bind_async("slow_distance",
        [](Point p1, Point p2, jsonrpc::ResponseCallback cb) {
            std::thread([p1, p2, cb = std::move(cb)]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                double dx = p1.x - p2.x;
                double dy = p1.y - p2.y;
                cb(std::sqrt(dx * dx + dy * dy));
            }).detach();
        });

    // Simulate async void work (e.g., fire-and-forget task)
    server.bind_async("slow_void",
        [](jsonrpc::ResponseCallback cb) {
            std::thread([cb = std::move(cb)]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                cb(nullptr);
            }).detach();
        });

    // Simulate async returning empty string
    server.bind_async("slow_empty_string",
        [](jsonrpc::ResponseCallback cb) {
            std::thread([cb = std::move(cb)]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                cb("");
            }).detach();
        });

    // --- Test cases: immediate async ---

    std::cout << "-- Immediate async (callback in same thread) --" << std::endl;

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":[3,5],"id":1})",
        R"("result":8)", "add(3,5) = 8");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":[-10,10],"id":2})",
        R"("result":0)", "add(-10,10) = 0");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"greet","params":["World"],"id":10})",
        R"("result":"Hello, World!")", "greet(World)");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"greet","params":[""],"id":11})",
        R"("result":"Hello, !")", "greet(empty string)");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"distance","params":[{"x":0,"y":0},{"x":3,"y":4}],"id":20})",
        R"("result":5.0)", "distance({0,0},{3,4}) = 5.0");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"ping","id":30})",
        R"("result":"pong")", "ping() = pong");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"void_func","id":40})",
        R"("result":null)", "void_func() → null");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"empty_string","id":41})",
        R"("result":"")", "empty_string() → \"\"");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"null_func","id":42})",
        R"("result":null)", "null_func() → null");

    send_expect_empty(server, r,
        R"({"jsonrpc":"2.0","method":"notify_log","params":["hello"]})",
        "notify_log (notification)");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"throw_error","params":[42],"id":50})",
        R"("error")", "throw_error → error response");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"throw_error","params":[42],"id":51})",
        R"("code":42)", "throw_error → error code 42");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":["a","b"],"id":60})",
        R"("error")", "add with wrong types → error");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":[1],"id":61})",
        R"("error")", "add with too few params → error");

    send_expect_empty(server, r,
        R"({"jsonrpc":"2.0","method":"nonexistent","params":[1]})",
        "notification to unknown method → no response");

    // --- Test cases: REAL ASYNC (callback from worker thread) ---

    std::cout << "-- Real async (callback from worker thread) --" << std::endl;

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"slow_add","params":[100,200],"id":100})",
        R"("result":300)", "slow_add(100,200) = 300 (async thread)");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"slow_add","params":[-5,5],"id":101})",
        R"("result":0)", "slow_add(-5,5) = 0 (async thread)");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"slow_greet","params":["Async"],"id":110})",
        R"("result":"Async Hello, Async!")", "slow_greet(Async) (async thread)");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"slow_distance","params":[{"x":0,"y":0},{"x":3,"y":4}],"id":120})",
        R"("result":5.0)", "slow_distance({0,0},{3,4}) = 5.0 (async thread)");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"slow_void","id":130})",
        R"("result":null)", "slow_void() → null (async thread)");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"slow_empty_string","id":131})",
        R"("result":"")", "slow_empty_string() → \"\" (async thread)");

    // --- Test: concurrent async requests ---

    std::cout << "-- Concurrent async requests --" << std::endl;

    // Fire multiple slow requests concurrently and wait for all
    {
        std::cout << "  [TEST] 3 concurrent slow_add requests" << std::endl;
        std::mutex mtx;
        std::condition_variable cv;
        int completed = 0;
        std::vector<std::string> responses(3);
        bool all_done = false;

        auto make_cb = [&](int idx) {
            return [&, idx](const std::string& resp) {
                std::lock_guard<std::mutex> lock(mtx);
                responses[idx] = resp;
                completed++;
                if (completed == 3) {
                    all_done = true;
                    cv.notify_one();
                }
            };
        };

        server.process(R"({"jsonrpc":"2.0","method":"slow_add","params":[1,1],"id":200})", make_cb(0));
        server.process(R"({"jsonrpc":"2.0","method":"slow_add","params":[2,2],"id":201})", make_cb(1));
        server.process(R"({"jsonrpc":"2.0","method":"slow_add","params":[3,3],"id":202})", make_cb(2));

        std::unique_lock<std::mutex> lock(mtx);
        if (cv.wait_for(lock, std::chrono::seconds(5), [&] { return all_done; })) {
            bool ok = true;
            if (responses[0].find(R"("result":2)") == std::string::npos) ok = false;
            if (responses[1].find(R"("result":4)") == std::string::npos) ok = false;
            if (responses[2].find(R"("result":6)") == std::string::npos) ok = false;
            for (int i = 0; i < 3; i++) {
                std::cout << "  <<< [" << i << "] " << responses[i] << std::endl;
            }
            if (ok) {
                std::cout << "  ✅ PASS" << std::endl;
                r.pass++;
            } else {
                std::cout << "  ❌ FAIL — unexpected results" << std::endl;
                r.fail++;
            }
        } else {
            std::cout << "  ❌ FAIL — timeout" << std::endl;
            r.fail++;
        }
        std::cout << std::endl;
    }

    // --- Test: async batch with slow handlers ---

    std::cout << "-- Async batch with slow handlers --" << std::endl;

    send_expect(server, r,
        R"([
            {"jsonrpc":"2.0","method":"slow_add","params":[10,20],"id":300},
            {"jsonrpc":"2.0","method":"slow_greet","params":["Batch"],"id":301},
            {"jsonrpc":"2.0","method":"notify_log","params":["batch notif"]}
        ])",
        R"("result":30)", "batch with slow_add + slow_greet + notification");

    r.report("Async + Array");
    std::cout << std::endl;
    return r;
}
