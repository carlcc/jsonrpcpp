#include "test_helper.hpp"

// ================================================================
//  Test: Sync handlers with named (object/dict) params
// ================================================================

TestResults test_sync_named() {
    std::cout << "======================================" << std::endl;
    std::cout << " Sync + Named (Dict) Params" << std::endl;
    std::cout << "======================================" << std::endl << std::endl;

    TestResults r;
    jsonrpc::Server server;

    // --- Register handlers with named params ---

    server.bind("add", [](int a, int b) -> int {
        return a + b;
    }, {"a", "b"});

    server.bind("subtract", [](int a, int b) -> int {
        return a - b;
    }, {"a", "b"});

    server.bind("greet", [](std::string name, std::string greeting) -> std::string {
        return greeting + ", " + name + "!";
    }, {"name", "greeting"});

    server.bind("distance", [](Point p1, Point p2) -> double {
        double dx = p1.x - p2.x;
        double dy = p1.y - p2.y;
        return std::sqrt(dx * dx + dy * dy);
    }, {"p1", "p2"});

    server.bind("void_func", [](int x) -> void {
        (void)x;
    }, {"x"});

    server.bind("empty_string", [](int x) -> std::string {
        (void)x;
        return "";
    }, {"x"});

    server.bind("null_func", [](int x) -> std::nullptr_t {
        (void)x;
        return nullptr;
    }, {"x"});

    server.bind("notify_log", [](std::string msg) -> void {
        std::cout << "  [LOG] " << msg << std::endl;
    }, {"msg"});

    server.bind("throw_error", [](int code, std::string msg) -> int {
        throw jsonrpc::JsonRpcError(code, msg);
        return 0;
    }, {"code", "msg"});

    // --- Test cases ---

    // 1. Basic named params
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":{"a":3,"b":5},"id":1})",
        R"("result":8)", "add(a=3,b=5) = 8");

    // 2. Order-independent named params
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":{"b":5,"a":3},"id":2})",
        R"("result":8)", "add(b=5,a=3) = 8 (reversed order)");

    // 3. Subtract with named params
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

    // 5. User-defined type with named params
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"distance","params":{"p1":{"x":0,"y":0},"p2":{"x":3,"y":4}},"id":20})",
        R"("result":5.0)", "distance(p1={0,0},p2={3,4}) = 5.0");

    // 6. Void return with named params → null
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"void_func","params":{"x":42},"id":30})",
        R"("result":null)", "void_func(x=42) → null");

    // 7. Empty string return with named params → ""
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"empty_string","params":{"x":1},"id":31})",
        R"("result":"")", "empty_string(x=1) → \"\"");

    // 8. Explicit null return with named params → null
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"null_func","params":{"x":1},"id":32})",
        R"("result":null)", "null_func(x=1) → null");

    // 9. Notification with named params
    send_expect_empty(server, r,
        R"({"jsonrpc":"2.0","method":"notify_log","params":{"msg":"dict notification"}})",
        "notify_log(msg=...) notification");

    // 10. Throw error with named params
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"throw_error","params":{"code":99,"msg":"oops"},"id":40})",
        R"("code":99)", "throw_error(code=99) → error code 99");

    // 11. Missing named param → error
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":{"a":3},"id":50})",
        R"("error")", "add(a=3) missing b → error");

    // 12. Wrong param type in named params → error
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":{"a":"x","b":"y"},"id":51})",
        R"("error")", "add(a='x',b='y') wrong types → error");

    // 13. Fallback to positional when no param names provided (but handler has names)
    //     Named-param handler should still accept array params via positional fallback
    send_expect(server, r,
        R"({"jsonrpc":"2.0","method":"add","params":[3,5],"id":60})",
        R"("result":8)", "add([3,5]) array fallback on named handler = 8");

    r.report("Sync + Named");
    std::cout << std::endl;
    return r;
}
