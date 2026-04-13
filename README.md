# jsonrpc++

一个 **header-only** 的 C++17 JSON-RPC 2.0 协议库。

> 纯协议实现，不绑定任何传输层（TCP / WebSocket / stdio …），通过异步回调接口 `process(string, callback)` 与你的 I/O 层无缝集成。

## ✨ 特性

- **Header-only** — 只需 `#include <jsonrpc/jsonrpc.hpp>` 即可使用
- **C++17** — 使用 `std::variant`、`std::optional`、`if constexpr` 等现代特性
- **模板元编程自动推导** — 注册任意签名函数，无需手动解析 JSON 参数
- **同步 & 异步 handler** — `bind()` 注册同步函数，`bind_async()` 注册异步函数
- **位置参数 & 命名参数** — 支持 JSON 数组和 JSON 对象两种传参方式
- **自定义类型** — 通过 nlohmann/json 的 ADL（`to_json` / `from_json`）直接使用你的类型
- **完整协议支持** — Request、Notification、Batch、标准错误码
- **传输层无关** — 只处理协议，不包含网络代码

## 📦 依赖

| 依赖 | 版本 | 说明 |
|------|------|------|
| [nlohmann/json](https://github.com/nlohmann/json) | ≥ 3.9 | JSON 解析与序列化 |
| C++17 编译器 | GCC 7+ / Clang 5+ / MSVC 19.14+ | 标准语言支持 |

## 🚀 快速开始

### 安装

jsonrpc++ 是 header-only 库，将 `include/` 目录加入你的 include 路径即可。

**使用 xmake：**

```lua
add_requires("nlohmann_json")

target("your_app")
    set_kind("binary")
    add_includedirs("path/to/jsonrpcpp/include")
    add_packages("nlohmann_json")
    add_files("src/*.cpp")
```

**使用 CMake：**

```cmake
# 添加 nlohmann_json（通过 FetchContent 或 find_package）
target_include_directories(your_app PRIVATE path/to/jsonrpcpp/include)
```

**直接复制：**

将 `include/jsonrpc/` 目录复制到你的项目中。

### 基本用法

```cpp
#include <jsonrpc/jsonrpc.hpp>
#include <iostream>

int main() {
    jsonrpc::Server server;

    // 注册同步 handler
    server.bind("add", [](int a, int b) -> int {
        return a + b;
    });

    // 处理 JSON-RPC 请求
    std::string request = R"({"jsonrpc":"2.0","method":"add","params":[3,5],"id":1})";

    server.process(request, [](const std::string& response) {
        std::cout << response << std::endl;
        // 输出: {"id":1,"jsonrpc":"2.0","result":8}
    });

    return 0;
}
```

## 📖 使用指南

### 注册同步 Handler

`bind()` 注册一个普通函数。函数参数从 JSON 自动解析，返回值自动序列化为 JSON。

```cpp
jsonrpc::Server server;

// 多参数
server.bind("add", [](int a, int b) -> int {
    return a + b;
});

// 字符串参数和返回值
server.bind("greet", [](std::string name) -> std::string {
    return "Hello, " + name + "!";
});

// 无参数
server.bind("ping", []() -> std::string {
    return "pong";
});

// void 返回值（result 为 null）
server.bind("log", [](std::string msg) -> void {
    std::cout << msg << std::endl;
});
```

### 注册异步 Handler

`bind_async()` 注册一个异步函数。函数的**最后一个参数**必须是 `jsonrpc::ResponseCallback`，通过它来传递结果。

```cpp
// 立即回调
server.bind_async("multiply",
    [](int a, int b, jsonrpc::ResponseCallback cb) {
        cb(a * b);
    });

// 在其他线程中完成计算后回调
server.bind_async("slow_compute",
    [](int x, jsonrpc::ResponseCallback cb) {
        std::thread([x, cb = std::move(cb)]() {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            cb(x * x);  // 在 worker 线程中回调
        }).detach();
    });
```

### 命名参数

通过传递参数名列表，handler 可以接受 JSON 对象（字典）形式的参数：

```cpp
// 注册时指定参数名
server.bind("subtract", [](int a, int b) -> int {
    return a - b;
}, {"a", "b"});

// 异步版本同理
server.bind_async("divide",
    [](double a, double b, jsonrpc::ResponseCallback cb) {
        cb(a / b);
    }, {"a", "b"});
```

客户端可以用任意顺序传递命名参数：

```json
{"jsonrpc":"2.0", "method":"subtract", "params":{"b":3, "a":10}, "id":1}
```

> **Fallback 机制**：注册了命名参数的 handler 同样支持数组形式的位置参数调用。

### 自定义类型

只需为你的类型实现 nlohmann/json 的 `to_json` / `from_json`：

```cpp
struct Point {
    double x, y;
};

// ADL 序列化
void to_json(nlohmann::json& j, const Point& p) {
    j = {{"x", p.x}, {"y", p.y}};
}

void from_json(const nlohmann::json& j, Point& p) {
    j.at("x").get_to(p.x);
    j.at("y").get_to(p.y);
}

// 直接使用自定义类型作为参数
server.bind("distance", [](Point p1, Point p2) -> double {
    double dx = p1.x - p2.x;
    double dy = p1.y - p2.y;
    return std::sqrt(dx * dx + dy * dy);
});
```

### 错误处理

在 handler 中抛出 `JsonRpcError` 来返回自定义错误：

```cpp
server.bind("divide", [](double a, double b) -> double {
    if (b == 0.0) {
        throw jsonrpc::JsonRpcError(-32000, "Division by zero");
    }
    return a / b;
});
```

抛出其他 `std::exception` 会被自动包装为 Internal Error（-32603）。

框架自动处理的协议错误：

| 错误码 | 含义 | 触发条件 |
|--------|------|----------|
| -32700 | Parse error | JSON 解析失败 |
| -32600 | Invalid Request | 缺少 `jsonrpc`/`method` 字段等 |
| -32601 | Method not found | 未注册的方法名 |
| -32602 | Invalid params | 参数类型错误、数量不足等 |
| -32603 | Internal error | handler 抛出非 JsonRpcError 异常 |

### Notification

当 JSON-RPC 请求不包含 `id` 字段时，它是一个 Notification。Server 会执行 handler 但**不返回任何响应**（回调收到空字符串）。

```json
{"jsonrpc":"2.0", "method":"log", "params":["something happened"]}
```

### Batch 请求

按照 JSON-RPC 2.0 规范，支持以数组形式发送批量请求：

```json
[
    {"jsonrpc":"2.0", "method":"add", "params":[1,2], "id":1},
    {"jsonrpc":"2.0", "method":"add", "params":[3,4], "id":2},
    {"jsonrpc":"2.0", "method":"log", "params":["hello"]}
]
```

- 每个请求独立处理，支持混合同步/异步 handler
- 如果 batch 中全是 notification，不产生响应
- 异步 batch 内部使用 `shared_ptr + atomic` 追踪所有 handler 完成后统一回调

### 集成到传输层

jsonrpc++ 不包含网络代码。你需要将它集成到你的 I/O 层：

**stdio 示例：**

```cpp
jsonrpc::Server server;
// ... 注册 handler ...

std::string line;
while (std::getline(std::cin, line)) {
    server.process(line, [](const std::string& response) {
        if (!response.empty()) {
            std::cout << response << std::endl;
        }
    });
}
```

**WebSocket 示例（伪代码）：**

```cpp
jsonrpc::Server server;
// ... 注册 handler ...

ws_server.on_message([&](auto connection, const std::string& msg) {
    server.process(msg, [connection](const std::string& response) {
        if (!response.empty()) {
            connection->send(response);
        }
    });
});
```

**TCP 示例（伪代码）：**

```cpp
jsonrpc::Server server;
// ... 注册 handler ...

tcp_server.on_data([&](auto client, const std::string& data) {
    server.process(data, [client](const std::string& response) {
        if (!response.empty()) {
            client->write(response);
        }
    });
});
```

## 🏗 项目结构

```
jsonrpcpp/
├── include/jsonrpc/
│   ├── jsonrpc.hpp           # 总入口，include 此文件即可
│   ├── types.hpp             # 核心类型：Request, Error, Id, Response 构造
│   ├── errors.hpp            # 标准错误码、JsonRpcError 异常、工厂函数
│   ├── function_traits.hpp   # 模板元编程：函数签名推导
│   ├── handler.hpp           # Handler 抽象、SyncHandler、AsyncHandler
│   ├── dispatcher.hpp        # 方法分发、handler 注册
│   └── server.hpp            # Server：JSON 解析、batch 处理、统一入口
├── tests/
│   ├── test_helper.hpp       # 测试公共框架
│   ├── main.cpp              # 测试入口
│   ├── test_sync_array.cpp   # 同步 + 数组参数 (16 tests)
│   ├── test_sync_named.cpp   # 同步 + 命名参数 (14 tests)
│   ├── test_async_array.cpp  # 异步 + 数组参数 (24 tests)
│   ├── test_async_named.cpp  # 异步 + 命名参数 (23 tests)
│   └── test_protocol.cpp     # 协议级测试 (16 tests)
└── xmake.lua                 # 构建配置
```

## 🧪 运行测试

```bash
# 使用 xmake
xmake -y
xmake run tests
```

测试覆盖 93 个用例，包括：

- **同步 handler**：位置参数、命名参数（含参数顺序无关、fallback 到数组）
- **异步 handler**：立即回调、真异步线程回调、并发请求、异步 batch
- **返回值类型**：int、string、自定义类型、void→null、空字符串→`""`、显式 null
- **错误场景**：handler 抛异常、参数类型错误、参数不足、缺失命名参数
- **协议级**：parse error、invalid request、method not found、batch 变体

## ⚙️ 架构设计

```
┌─────────────────────────────────────────────────────┐
│                    Server                           │
│  process(json_string, callback)                     │
│    ├── JSON 解析                                     │
│    ├── 单请求 / Batch 分流                            │
│    └── parse error / invalid request 处理            │
├─────────────────────────────────────────────────────┤
│                  Dispatcher                         │
│  dispatch(Request, DispatchCallback)                │
│    ├── 方法查找                                      │
│    ├── Notification 静默处理                          │
│    └── 异常捕获 → Error Response                      │
├─────────────────────────────────────────────────────┤
│                   Handler                           │
│  invoke(params, InternalCallback)                   │
│    ├── SyncHandler<F>: 调用函数，立即回调              │
│    └── AsyncHandler<F>: 调用函数，用户在任意时机回调    │
├─────────────────────────────────────────────────────┤
│              function_traits<F>                      │
│  编译期推导 return_type、argument_tuple、arity         │
└─────────────────────────────────────────────────────┘
```

### 三层回调设计

| 层级 | 类型 | 用途 |
|------|------|------|
| **用户层** | `ResponseCallback = std::function<void(json)>` | 异步 handler 的回调签名，API 简洁 |
| **内部层** | `InternalCallback = std::function<void(HandlerResult)>` | `HandlerResult = std::optional<json>`，区分 void/null/值 |
| **分发层** | `DispatchCallback = std::function<void(std::optional<json>)>` | 区分 notification（无响应）和有效响应 |
