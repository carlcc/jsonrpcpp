#include "test_helper.hpp"

// ================================================================
//  Test: Sync handlers with positional (array) params
// ================================================================

TestResults test_sync_array() {
    std::cout << "======================================" << std::endl;
    std::cout << " Sync + Array Params" << std::endl;
    std::cout << "======================================" << std::endl << std::endl;

    TestResults r;
    jsonrpc::Server server;

    // --- Register handlers ---

    server.bind("add", [](int a, int b) -> int {
        return a + b;
    });

    server.bind("greet", [](std::string name) -> std::string {
        return "Hello, " + name + "!";
    });

    server.bind("distance", [](Point p1, Point p2) -> double {
        double dx = p1.x - p2.x;
        double dy = p1.y - p2.y;
        return std::sqrt(dx * dx + dy * dy);
    });

    server.bind("ping", []() -> std::string {
        return "pong";
    });

    server.bind("void_func", []() -> void {});

    server.bind("empty_string", []() -> std::string {
        return "";
    });

    server.bind("null_func", []() -> std::nullptr_t {
        return nullptr;
    });

    server.bind("notify_log", [](std::string msg) -> void {
        std::cout << "  [LOG] " << msg << std::endl;
    });

    server.bind("throw_error", [](int code) -> int {
        throw jsonrpc::JsonRpcError(code, "sync array error");
        return 0;
    });

    // --- Test cases ---

    // 1. Multi-param integer computation
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":[3,5],"id":1})",
        R"("result":8)", "add(3,5) = 8");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":[-10,10],"id":2})",
        R"("result":0)", "add(-10,10) = 0");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":[0,0],"id":3})",
        R"("result":0)", "add(0,0) = 0");

    // 2. String param and return
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"greet","params":["World"],"id":10})",
        R"("result":"Hello, World!")", "greet(World)");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"greet","params":[""],"id":11})",
        R"("result":"Hello, !")", "greet(empty string)");

    // 3. User-defined type
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"distance","params":[{"x":0,"y":0},{"x":3,"y":4}],"id":20})",
        R"("result":5.0)", "distance({0,0},{3,4}) = 5.0");

    // 4. No-arg function
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"ping","id":30})",
        R"("result":"pong")", "ping() = pong");

    // 5. Void return → null
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"void_func","id":40})",
        R"("result":null)", "void_func() → null");

    // 6. Empty string return → ""
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"empty_string","id":41})",
        R"("result":"")", "empty_string() → \"\"");

    // 7. Explicit null return → null
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"null_func","id":42})",
        R"("result":null)", "null_func() → null");

    // 8. Notification (no id → no response)
    send_expect_empty(server, r,
        R"({"jsonrpc":"2.0","method":"notify_log","params":["hello"]})",
        "notify_log (notification)");

    // 9. Handler throws error
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"throw_error","params":[42],"id":50})",
        R"("error")", "throw_error → error response");

    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"throw_error","params":[42],"id":51})",
        R"("code":42)", "throw_error → error code 42");

    // 10. Wrong param types → error
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":["a","b"],"id":60})",
        R"("error")", "add with wrong types → error");

    // 11. Too few params → error
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":[1],"id":61})",
        R"("error")", "add with too few params → error");

    // 12. Notification for unknown method → no response
    send_expect_empty(server, r,
        R"({"jsonrpc":"2.0","method":"nonexistent","params":[1]})",
        "notification to unknown method → no response");

    r.report("Sync + Array");
    std::cout << std::endl;
    return r;
}
