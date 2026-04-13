#include "test_helper.hpp"

// Forward declarations from each test file
TestResults test_sync_array();
TestResults test_sync_named();
TestResults test_async_array();
TestResults test_async_named();
TestResults test_protocol();

int main() {
    std::cout << "==========================================" << std::endl;
    std::cout << " JSON-RPC 2.0 Test Suite" << std::endl;
    std::cout << "==========================================" << std::endl << std::endl;

    struct Suite {
        std::string name;
        TestSuiteFn fn;
    };

    std::vector<Suite> suites = {
        {"Sync + Array",  test_sync_array},
        {"Sync + Named",  test_sync_named},
        {"Async + Array", test_async_array},
        {"Async + Named", test_async_named},
        {"Protocol",      test_protocol},
    };

    int total_pass = 0;
    int total_fail = 0;

    for (auto& suite : suites) {
        TestResults r = suite.fn();
        total_pass += r.pass;
        total_fail += r.fail;
    }

    std::cout << "==========================================" << std::endl;
    std::cout << " TOTAL: " << total_pass << " passed, "
              << total_fail << " failed" << std::endl;
    std::cout << "==========================================" << std::endl;

    return total_fail > 0 ? 1 : 0;
}
