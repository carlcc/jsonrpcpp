#include "test_helper.hpp"
#include <thread>
#include <chrono>

// ================================================================
//  Test: Async handlers with named (object/dict) params
//  Includes tests with real async work on separate threads
// ================================================================

TestResults test_async_named() {
    std::cout << "======================================" << std::endl;
    std::cout << " Async + Named (Dict) Params" << std::endl;
    std::cout << "======================================" << std::endl << std::endl;

    TestResults r;
    jsonrpc::Server server;

    // --- Register handlers with named params (immediate callback) ---

    server.bind_async("add",
        [](int a, int b, jsonrpc::ResponseCallback cb) {
            cb(a + b);
        }, {"a", "b"});

    server.bind_async("subtract",
        [](int a, int b, jsonrpc::ResponseCallback cb) {
            cb(a - b);
        }, {"a", "b"});

    server.bind_async("greet",
        [](std::string name, std::string greeting, jsonrpc::ResponseCallback cb) {
            cb(greeting + ", " + name + "!");
        }, {"name", "greeting"});

    server.bind_async("distance",
        [](Point p1, Point p2, jsonrpc::ResponseCallback cb) {
            double dx = p1.x - p2.x;
            double dy = p1.y - p2.y;
            cb(std::sqrt(dx * dx + dy * dy));
        }, {"p1", "p2"});

    server.bind_async("void_func",
        [](int x, jsonrpc::ResponseCallback cb) {
            (void)x;
            cb(nullptr);
        }, {"x"});

    server.bind_async("empty_string",
        [](int x, jsonrpc::ResponseCallback cb) {
            (void)x;
            cb("");
        }, {"x"});

    server.bind_async("null_func",
        [](int x, jsonrpc::ResponseCallback cb) {
            (void)x;
            cb(nullptr);
        }, {"x"});

    server.bind_async("notify_log",
        [](std::string msg, jsonrpc::ResponseCallback cb) {
            std::cout << "  [ASYNC LOG] " << msg << std::endl;
            cb(nullptr);
        }, {"msg"});

    server.bind_async("throw_error",
        [](int code, std::string msg, jsonrpc::ResponseCallback /*cb*/) {
            throw jsonrpc::JsonRpcError(code, msg);
        }, {"code", "msg"});

    // --- Register handlers with named params (REAL ASYNC: worker thread) ---

    server.bind_async("slow_add",
        [](int a, int b, jsonrpc::ResponseCallback cb) {
            std::thread([a, b, cb = std::move(cb)]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                cb(a + b);
            }).detach();
        }, {"a", "b"});

    server.bind_async("slow_greet",
        [](std::string name, std::string greeting, jsonrpc::ResponseCallback cb) {
            std::thread([name = std::move(name), greeting = std::move(greeting),
                         cb = std::move(cb)]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                cb(greeting + ", " + name + "!");
            }).detach();
        }, {"name", "greeting"});

    server.bind_async("slow_distance",
        [](Point p1, Point p2, jsonrpc::ResponseCallback cb) {
            std::thread([p1, p2, cb = std::move(cb)]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                double dx = p1.x - p2.x;
                double dy = p1.y - p2.y;
                cb(std::sqrt(dx * dx + dy * dy));
            }).detach();
        }, {"p1", "p2"});

    server.bind_async("slow_void",
        [](int x, jsonrpc::ResponseCallback cb) {
            std::thread([x, cb = std::move(cb)]() {
                (void)x;
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                cb(nullptr);
            }).detach();
        }, {"x"});

    server.bind_async("slow_empty_string",
        [](int x, jsonrpc::ResponseCallback cb) {
            std::thread([x, cb = std::move(cb)]() {
                (void)x;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                cb("");
            }).detach();
        }, {"x"});

    // --- Test cases: immediate async with named params ---

    std::cout << "-- Immediate async with named params --" << std::endl;

    // 1. Basic named params
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":{"a":3,"b":5},"id":1})",
        R"("result":8)", "add(a=3,b=5) = 8");

    // 2. Order-independent
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":{"b":5,"a":3},"id":2})",
        R"("result":8)", "add(b=5,a=3) = 8 (reversed)");

    // 3. Subtract
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"subtract","params":{"a":10,"b":3},"id":3})",
        R"("result":7)", "subtract(a=10,b=3) = 7");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"subtract","params":{"b":3,"a":10},"id":4})",
        R"("result":7)", "subtract(b=3,a=10) = 7 (reversed)");

    // 4. Multi-string named params
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"greet","params":{"name":"World","greeting":"Hello"},"id":10})",
        R"("result":"Hello, World!")", "greet(name=World,greeting=Hello)");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"greet","params":{"greeting":"Hi","name":"Alice"},"id":11})",
        R"("result":"Hi, Alice!")", "greet(greeting=Hi,name=Alice) reversed");

    // 5. User-defined type
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"distance","params":{"p1":{"x":0,"y":0},"p2":{"x":3,"y":4}},"id":20})",
        R"("result":5.0)", "distance(p1={0,0},p2={3,4}) = 5.0");

    // 6. Void return
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"void_func","params":{"x":42},"id":30})",
        R"("result":null)", "void_func(x=42) → null");

    // 7. Empty string return
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"empty_string","params":{"x":1},"id":31})",
        R"("result":"")", "empty_string(x=1) → \"\"");

    // 8. Explicit null return
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"null_func","params":{"x":1},"id":32})",
        R"("result":null)", "null_func(x=1) → null");

    // 9. Notification
    send_expect_empty(server, r,
        R"({"jsonrpc":"2.0","method":"notify_log","params":{"msg":"dict notif"}})",
        "notify_log(msg=...) notification");

    // 10. Throw error
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"throw_error","params":{"code":99,"msg":"oops"},"id":40})",
        R"("code":99)", "throw_error(code=99) → error code 99");

    // 11. Missing named param → error
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":{"a":3},"id":50})",
        R"("error")", "add(a=3) missing b → error");

    // 12. Wrong param type → error
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":{"a":"x","b":"y"},"id":51})",
        R"("error")", "add(a='x',b='y') wrong types → error");

    // 13. Positional array fallback on named handler
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":[3,5],"id":60})",
        R"("result":8)", "add([3,5]) array fallback on named handler = 8");

    // --- Test cases: REAL ASYNC with named params (worker thread) ---

    std::cout << "-- Real async with named params (worker thread) --" << std::endl;

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"slow_add","params":{"a":100,"b":200},"id":100})",
        R"("result":300)", "slow_add(a=100,b=200) = 300 (async)");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"slow_add","params":{"b":200,"a":100},"id":101})",
        R"("result":300)", "slow_add(b=200,a=100) = 300 (async, reversed)");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"slow_greet","params":{"name":"Async","greeting":"Hi"},"id":110})",
        R"("result":"Hi, Async!")", "slow_greet(name=Async,greeting=Hi) (async)");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"slow_distance","params":{"p1":{"x":0,"y":0},"p2":{"x":3,"y":4}},"id":120})",
        R"("result":5.0)", "slow_distance(p1={0,0},p2={3,4}) = 5.0 (async)");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"slow_void","params":{"x":1},"id":130})",
        R"("result":null)", "slow_void(x=1) → null (async)");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"slow_empty_string","params":{"x":1},"id":131})",
        R"("result":"")", "slow_empty_string(x=1) → \"\" (async)");

    // --- Test: concurrent async requests with named params ---

    std::cout << "-- Concurrent async requests with named params --" << std::endl;

    {
        std::cout << "  [TEST] 3 concurrent slow_add requests (named params)" << std::endl;
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

        server.process(R"({"jsonrpc":"2.0","method":"slow_add","params":{"a":1,"b":1},"id":200})", make_cb(0));
        server.process(R"({"jsonrpc":"2.0","method":"slow_add","params":{"a":2,"b":2},"id":201})", make_cb(1));
        server.process(R"({"jsonrpc":"2.0","method":"slow_add","params":{"a":3,"b":3},"id":202})", make_cb(2));

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

    // --- Test: async batch with slow named-param handlers ---

    std::cout << "-- Async batch with slow named-param handlers --" << std::endl;

    send_expect(server, r,
        R"([
            {"jsonrpc":"2.0","method":"slow_add","params":{"a":10,"b":20},"id":300},
            {"jsonrpc":"2.0","method":"slow_greet","params":{"name":"Batch","greeting":"Hey"},"id":301},
            {"jsonrpc":"2.0","method":"notify_log","params":{"msg":"batch notif"}}
        ])",
        R"("result":30)", "batch with slow_add + slow_greet + notification (named)");

    r.report("Async + Named");
    std::cout << std::endl;
    return r;
}
