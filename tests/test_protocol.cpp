#include "test_helper.hpp"

// ================================================================
//  Test: Protocol-level tests (parse error, invalid request, batch)
//  These are transport-level and don't depend on sync/async
// ================================================================

TestResults test_protocol() {
    std::cout << "======================================" << std::endl;
    std::cout << " Protocol-level Tests" << std::endl;
    std::cout << "======================================" << std::endl << std::endl;

    TestResults r;
    jsonrpc::Server server;

    // Register a minimal handler for batch tests
    server.bind("echo", [](int x) -> int { return x; });
    server.bind("void_func", []() -> void {});
    server.bind_async("async_echo",
        [](int x, jsonrpc::ResponseCallback cb) { cb(x); });

    // --- Parse error ---
    std::cout << "-- Parse errors --" << std::endl;

    send_expect(server, r,
        R"({invalid json})",
        R"(-32700)", "Malformed JSON → -32700");

    send_expect(server, r,
        R"()",
        R"(-32700)", "Empty string → -32700");

    send_expect(server, r,
        R"({)",
        R"(-32700)", "Incomplete JSON → -32700");

    // --- Invalid request ---
    std::cout << "-- Invalid request --" << std::endl;

    send_expect(server, r,
        R"({"method":"echo","params":[1],"id":1})",
        R"(-32600)", "Missing jsonrpc field → -32600");

    send_expect(server, r,
        R"({"jsonrpc":"1.0","method":"echo","params":[1],"id":1})",
        R"(-32600)", "Wrong jsonrpc version → -32600");

    send_expect(server, r,
        R"(42)",
        R"(-32600)", "Non-object/non-array → -32600");

    send_expect(server, r,
        R"("hello")",
        R"(-32600)", "String instead of object → -32600");

    // --- Method not found ---
    std::cout << "-- Method not found --" << std::endl;

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"nonexistent","params":[],"id":1})",
        R"(-32601)", "Unknown method → -32601");

    send_expect_empty(server, r,
        R"({"jsonrpc":"2.0","method":"nonexistent","params":[1]})",
        "Notification to unknown method → no response");

    // --- Batch: empty ---
    std::cout << "-- Batch: empty --" << std::endl;

    send_expect(server, r,
        R"([])",
        R"(-32600)", "Empty batch → -32600");

    // --- Batch: all valid ---
    std::cout << "-- Batch: all valid --" << std::endl;

    send_expect(server, r,
        R"([
            {"jsonrpc":"2.0","method":"echo","params":[1],"id":10},
            {"jsonrpc":"2.0","method":"echo","params":[2],"id":11},
            {"jsonrpc":"2.0","method":"async_echo","params":[3],"id":12}
        ])",
        R"("result":1)", "Batch 3 valid requests");

    // --- Batch: mixed with notifications ---
    std::cout << "-- Batch: mixed with notifications --" << std::endl;

    send_expect(server, r,
        R"([
            {"jsonrpc":"2.0","method":"echo","params":[42],"id":20},
            {"jsonrpc":"2.0","method":"void_func"},
            {"jsonrpc":"2.0","method":"nonexistent","id":21}
        ])",
        R"("result":42)", "Batch: request + notification + not-found");

    // --- Batch: all notifications ---
    std::cout << "-- Batch: all notifications --" << std::endl;

    send_expect_empty(server, r,
        R"([
            {"jsonrpc":"2.0","method":"void_func"},
            {"jsonrpc":"2.0","method":"void_func"}
        ])",
        "Batch of all notifications → no response");

    // --- Batch: invalid items ---
    std::cout << "-- Batch: invalid items --" << std::endl;

    send_expect(server, r,
        R"([1, "hello", null])",
        R"(-32600)", "Batch of non-objects → -32600 errors");

    send_expect(server, r,
        R"([1, {"jsonrpc":"2.0","method":"echo","params":[99],"id":30}])",
        R"("result":99)", "Batch: invalid item + valid request");

    // --- Batch: single item ---
    std::cout << "-- Batch: single item --" << std::endl;

    send_expect(server, r,
        R"([{"jsonrpc":"2.0","method":"echo","params":[7],"id":40}])",
        R"("result":7)", "Batch with single item");

    r.report("Protocol");
    std::cout << std::endl;
    return r;
}
