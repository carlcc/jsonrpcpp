# jsonrpc++ 设计文档

> 版本：1.0 | 最后更新：2026-04-13

## 1. 设计目标

### 1.1 核心目标

| 目标 | 描述 |
|------|------|
| **协议完备** | 完整实现 JSON-RPC 2.0 规范，包括 Request、Notification、Batch、标准错误码 |
| **零摩擦集成** | Header-only，无需编译、链接，复制即用 |
| **传输层无关** | 只做协议层，不绑定任何 I/O 框架，通过一个 `process(string, callback)` 接口适配任意传输 |
| **类型安全** | 利用 C++17 模板元编程，在编译期推导参数类型，用户无需手动解析 JSON |
| **同步/异步统一** | 同步和异步 handler 共享同一套分发链路，对上层透明 |

### 1.2 非目标

以下内容**有意不包含**在本库中：

- ❌ 传输层实现（TCP、WebSocket、HTTP、stdio 等）
- ❌ 连接管理、会话状态
- ❌ 客户端功能（只实现 Server 端）
- ❌ JSON-RPC 1.0 兼容
- ❌ 多线程调度器 / 线程池（留给集成方决策）

## 2. 关键设计决策

### 2.1 只提供异步 `process()` API

**决策**：Server 只暴露 `process(const string&, ProcessCallback)` 一个入口，不提供同步版 `string process(const string&)`。

**原因**：

1. 如果同时提供同步和异步 API，用户需要理解"何时用哪个"，增加认知负担
2. 异步 API 是同步的超集：同步 handler 会立即调用 callback，行为等同于同步
3. 提供同步 API 时，无法支持真正的异步 handler（`bind_async` 注册的函数），因为函数返回时结果可能尚未就绪
4. 统一 API 使传输层集成代码更简单——只需一个模式

**代价与缓解**：

- 纯同步场景下，callback 模式稍显冗余，但单行 lambda 即可解决
- 如果未来确实需要，可以在此基础上用 `std::promise/future` 封装出同步版本

### 2.2 `bind()` vs `bind_async()` 显式分离

**决策**：用两个不同的注册函数区分同步和异步 handler，而非通过函数签名自动判断。

**备选方案**：

| 方案 | 优缺点 |
|------|--------|
| A. 自动检测最后一个参数是否为 `ResponseCallback` | ❌ 用户的函数恰好有 `std::function<void(json)>` 参数时会误判 |
| B. 全部强制异步（所有 handler 接受 callback） | ❌ 简单同步函数变得冗余：`[](int a, int b, auto cb) { cb(a+b); }` |
| **C. 显式 `bind()` / `bind_async()`** | ✅ 语义清晰，无歧义，两种模式各自简洁 |

选择方案 C。

### 2.3 三层回调类型

这是本库最重要的设计决策，解决了"void 返回值"、"显式 null"和"Notification 静默"三种语义的正确区分。

#### 问题

JSON-RPC 2.0 规范要求：
- **Request**：必须有响应，`"result": null` 是合法的（handler 返回 void 时）
- **Notification**：必须**没有**响应，即使 handler 抛出异常
- 用户可以**显式返回** `null`、空字符串 `""`、或 `0`，这些和 void 返回是不同的语义

如果只用 `std::function<void(json)>` 一种 callback，无法区分：
- void 返回（应映射为 `"result": null`）
- 显式返回 `json(nullptr)`（也是 `"result": null`，但语义不同）
- Notification（不应有任何响应）

#### 解决方案：三层回调

```
层级 1 — ResponseCallback = std::function<void(json)>
  用途：异步 handler 的用户 API
  语义：用户调用 cb(value) 传递结果，cb(nullptr) 表示显式 null
  设计：最简洁，用户不需要关心 optional

层级 2 — InternalCallback = std::function<void(HandlerResult)>
  HandlerResult = std::optional<json>
  用途：Handler → Dispatcher 的内部回调
  语义：nullopt = void 返回，json(nullptr) = 显式 null，json(x) = 值
  设计：Dispatcher 据此决定响应中的 result 字段

层级 3 — DispatchCallback = std::function<void(std::optional<json>)>
  用途：Dispatcher → Server 的回调
  语义：nullopt = Notification（不回传），json = 完整响应对象
  设计：Server 据此决定是否调用 ProcessCallback
```

**类型转换链**：

```
AsyncHandler:
  ResponseCallback(json_val)
    → InternalCallback(HandlerResult(json_val))
    
SyncHandler (非 void):
  auto result = func(args...)
    → InternalCallback(HandlerResult(json(result)))

SyncHandler (void):
  func(args...)
    → InternalCallback(std::nullopt)

Dispatcher (Request):
  InternalCallback(result)
    → result.has_value() ? make_response(id, *result) : make_response(id, null)
    → DispatchCallback(json_response)

Dispatcher (Notification):
  → 执行 handler（忽略结果）
  → DispatchCallback(std::nullopt)
```

### 2.4 命名参数的 Fallback 机制

**决策**：注册了命名参数的 handler 同时支持数组形式调用。

```cpp
server.bind("subtract", [](int a, int b) { return a - b; }, {"a", "b"});

// 以下两种调用方式都有效：
// {"params": {"a": 10, "b": 3}}     → 命名参数
// {"params": [10, 3]}               → 位置参数（fallback）
```

**实现**：

```cpp
if (!param_names_.empty() && params.is_object()) {
    return extract_args_named<...>(params, param_names_, seq);
} else {
    return extract_args_array<...>(params, seq);
}
```

运行时根据两个条件判断：
1. handler 是否注册了参数名（`!param_names_.empty()`）
2. 请求参数是否为对象（`params.is_object()`）

两者同时满足才走命名路径，否则走数组路径。这使得同一个 handler 可以同时服务使用不同参数格式的客户端。

### 2.5 Batch 的异步完成追踪

**问题**：Batch 中的每个请求可能绑定到异步 handler，完成时间不确定。需要一种机制在所有请求都完成后统一回调。

**方案**：

```cpp
struct BatchState {
    std::mutex mutex;
    std::vector<json> responses;
    std::size_t total;
    std::atomic<std::size_t> completed{0};
    ProcessCallback callback;

    void add_response(std::optional<json> resp);
};
auto state = std::make_shared<BatchState>(...);
```

- `shared_ptr<BatchState>` 被每个请求的回调 lambda 捕获，保证生命周期
- `std::mutex` 保护 `responses` 向量的并发写入
- `std::atomic<size_t>` 做无锁完成计数
- 当 `completed + 1 == total` 时，最后一个完成者负责调用最终 callback
- Notification 不产生响应（`resp == nullopt`），但仍计入完成计数

**为什么不用 `std::barrier` / `std::latch`？**
- 它们是 C++20 特性，本库要求 C++17
- `atomic + shared_ptr` 方案足够简单、可靠

### 2.6 Notification 的静默处理

**规范要求**：JSON-RPC 2.0 Notification 不应产生任何响应，即使 handler 抛出异常。

**实现**：

在 Dispatcher 中，notification 的处理分支：

```cpp
if (req.is_notification()) {
    try {
        handler->invoke(req.params, [](HandlerResult) {
            // discard result
        });
    } catch (...) {
        // 吞掉所有异常
    }
    cb(std::nullopt);  // 告知 Server：无响应
    return;
}
```

在 Server 中：

```cpp
dispatcher_.dispatch(req, [cb](std::optional<json> response) {
    if (!response.has_value()) {
        cb("");  // 空字符串 = 无需回传
        return;
    }
    cb(response->dump());
});
```

应用层收到空字符串即可忽略。

## 3. 模板元编程技术

### 3.1 function_traits 的推导链

```
用户传入 lambda
  → function_traits<Lambda>
  → 主模板匹配 decltype(&Lambda::operator())
  → 得到 R(C::*)(Args...) const
  → 特化匹配 R(C::*)(Args...) const
  → 归约到 function_traits<R(Args...)>
  → 提取 return_type, argument_tuple, arity
```

对于 `noexcept` 函数，C++17 将 `noexcept` 作为类型系统的一部分，因此需要额外的特化。本库为每种函数形式都提供了 `noexcept` 变体。

### 3.2 编译期参数提取

```cpp
template <typename Tuple, std::size_t... Is>
Tuple extract_args_array(const json& params, std::index_sequence<Is...>) {
    return Tuple{params[Is].template get<std::tuple_element_t<Is, Tuple>>()...};
}
```

这行代码展开后，对于 `Tuple = tuple<int, string, double>`，等价于：

```cpp
return tuple<int, string, double>{
    params[0].get<int>(),
    params[1].get<string>(),
    params[2].get<double>()
};
```

每个参数的类型转换代码在编译期生成，运行时只执行实际的 JSON → C++ 值提取。

### 3.3 remove_last_t 递归剥离

```cpp
template <typename Head, typename... Tail>
struct remove_last<std::tuple<Head, Tail...>> {
    using type = decltype(std::tuple_cat(
        std::declval<std::tuple<Head>>(),
        std::declval<typename remove_last<std::tuple<Tail...>>::type>()
    ));
};

template <typename T>
struct remove_last<std::tuple<T>> {
    using type = std::tuple<>;  // 基础情况：只剩一个元素，返回空 tuple
};
```

对于 `tuple<int, string, ResponseCallback>`：

```
remove_last<tuple<int, string, ResponseCallback>>
  = tuple_cat(tuple<int>, remove_last<tuple<string, ResponseCallback>>::type)
  = tuple_cat(tuple<int>, tuple_cat(tuple<string>, remove_last<tuple<ResponseCallback>>::type))
  = tuple_cat(tuple<int>, tuple_cat(tuple<string>, tuple<>))
  = tuple_cat(tuple<int>, tuple<string>)
  = tuple<int, string>
```

### 3.4 if constexpr 分支

`if constexpr` 使得 void 和非 void 返回值的处理代码可以共存于同一个模板中，编译器只实例化对应分支：

```cpp
HandlerResult invoke_impl(const json& params) {
    auto args = extract_args(params);
    if constexpr (std::is_void_v<return_type>) {
        std::apply(func_, std::move(args));
        return std::nullopt;  // ← 此分支只在 void 时编译
    } else {
        return json(std::apply(func_, std::move(args)));  // ← 此分支只在非 void 时编译
    }
}
```

同样，`if constexpr (arity == 0)` 避免了对零参数函数执行无意义的参数提取。

## 4. 协议合规性

### 4.1 JSON-RPC 2.0 规范遵从情况

| 规范要求 | 实现状态 | 实现位置 |
|----------|----------|----------|
| 请求必须包含 `"jsonrpc": "2.0"` | ✅ `parse_request()` 校验 | types.hpp |
| 请求必须包含 `"method"` 字符串 | ✅ `parse_request()` 校验 | types.hpp |
| `"params"` 可选，必须为 array 或 object | ✅ `parse_request()` 校验 | types.hpp |
| `"id"` 可以是 string、number、null | ✅ `Id = variant<nullptr_t, int64_t, string>` | types.hpp |
| 无 `"id"` 字段 → Notification | ✅ `Request::id` 为 `optional<Id>` | types.hpp |
| Notification 不应有响应 | ✅ Dispatcher 静默处理 | dispatcher.hpp |
| Notification 异常不产生响应 | ✅ `catch (...)` 吞掉 | dispatcher.hpp |
| 响应必须包含 `"jsonrpc": "2.0"` | ✅ `make_response()` | types.hpp |
| 成功响应包含 `"result"`，不含 `"error"` | ✅ `make_response()` | types.hpp |
| 错误响应包含 `"error"`，不含 `"result"` | ✅ `make_error_response()` | types.hpp |
| 错误对象包含 `"code"` 和 `"message"` | ✅ `Error::to_json()` | types.hpp |
| 错误 `"data"` 字段可选 | ✅ 只在 `!data.is_null()` 时包含 | types.hpp |
| Batch 请求为 JSON 数组 | ✅ `process_batch()` | server.hpp |
| 空 Batch 为 Invalid Request | ✅ 空数组检查 | server.hpp |
| Batch 中每项独立处理 | ✅ 逐项 dispatch | server.hpp |
| Batch 全为 Notification → 无响应 | ✅ `responses.empty()` 检查 | server.hpp |
| 标准错误码 -32700 ~ -32603 | ✅ `error_code` 命名空间 | errors.hpp |

### 4.2 Id 处理细节

JSON-RPC 2.0 规范规定 id 可以是 String、Number（不含小数）或 Null。本库的 `Id` 类型使用 `int64_t` 而非 `double` 来存储数值 id，因为：

1. 规范建议不使用小数
2. 整数比较更可靠（无浮点精度问题）
3. `int64_t` 覆盖了所有实际使用场景

`id_from_json()` 只在 `j.is_number_integer()` 时解析，非整数的数值 id 会退化为 `nullptr`。

## 5. 类型系统设计

### 5.1 自定义类型支持

本库通过 nlohmann/json 的 ADL（Argument-Dependent Lookup）机制支持自定义类型：

```cpp
// 用户定义
struct Point { double x, y; };

void to_json(json& j, const Point& p) { ... }
void from_json(const json& j, Point& p) { ... }

// 直接使用
server.bind("distance", [](Point p1, Point p2) -> double { ... });
```

这得益于 `json::get<T>()` 内部调用 `from_json()`，以及 `json(value)` 内部调用 `to_json()`。整个链路对自定义类型完全透明。

### 5.2 支持的参数/返回值类型

任何满足以下条件的类型 `T` 都可以用作 handler 参数或返回值：

| 条件 | 说明 |
|------|------|
| `json::get<T>()` 有效 | 可从 JSON 反序列化（用于参数） |
| `json(T)` 有效 | 可序列化为 JSON（用于返回值） |

内建支持的类型包括：`bool`、整数类型、浮点类型、`std::string`、`std::vector<T>`、`std::map<std::string, T>`、`nlohmann::json` 本身等。

## 6. 错误处理哲学

### 6.1 分层错误处理

```
异常向上传播              错误响应向下回传
─────────────>          <───────────────

Handler 层:
  json 解析异常 → 包装为 JsonRpcError(invalid_params)
  其他 std::exception → 包装为 JsonRpcError(internal_error)
  JsonRpcError → 直接上抛

Dispatcher 层:
  方法未找到 → 直接生成 Error Response
  捕获 JsonRpcError → 提取 Error 对象构造响应
  捕获 std::exception → 构造 internal_error 响应

Server 层:
  json::parse_error → 直接生成 parse_error 响应
  parse_request 失败 → 直接生成 invalid_request 响应
```

### 6.2 设计原则

1. **任何输入都不会使 Server 崩溃**：所有可能的异常都被捕获并转换为错误响应
2. **错误码精确**：不同类型的异常映射到不同的标准错误码
3. **用户可控**：handler 中抛出 `JsonRpcError` 可以自定义错误码和消息
4. **异常信息保留**：异常的 `what()` 被放入 Error 的 `data` 或 `message` 字段，便于调试

### 6.3 用户自定义错误

```cpp
server.bind("divide", [](double a, double b) -> double {
    if (b == 0.0) {
        throw jsonrpc::JsonRpcError(-32000, "Division by zero");
    }
    return a / b;
});
```

`JsonRpcError` 继承 `std::runtime_error`，第三个参数 `data` 可选，用于附加结构化调试信息：

```cpp
throw jsonrpc::JsonRpcError(-32000, "Validation failed", json{
    {"field", "email"},
    {"reason", "Invalid format"}
});
```

## 7. 可扩展性设计

### 7.1 当前扩展点

| 扩展方式 | 说明 |
|----------|------|
| **自定义传输层** | 实现 `process(string, callback)` 的调用端即可 |
| **自定义类型** | 实现 `to_json` / `from_json` ADL |
| **自定义错误** | 抛出 `JsonRpcError` 并指定错误码 |
| **中间件模式** | 在 `process()` 前后包装 lambda 即可实现日志、鉴权等 |

### 7.2 潜在的扩展方向

以下是设计上预留了扩展空间但尚未实现的方向：

| 方向 | 可行方案 |
|------|----------|
| **中间件链** | 在 Dispatcher 中增加 `before_dispatch` / `after_dispatch` 钩子 |
| **方法自省** | 利用已存储的 `param_names_` 实现 `rpc.discover` |
| **客户端** | 复用 types.hpp 和 errors.hpp，新增 Client 类 |
| **连接上下文** | 扩展 `invoke()` 签名传入 context 对象 |
| **取消支持** | 扩展 `ResponseCallback` 返回 cancellation token |

### 7.3 中间件模式示例

虽然没有内置中间件系统，但通过 lambda 包装可以轻松实现：

```cpp
jsonrpc::Server server;
// ... 注册 handler ...

// 日志中间件
auto logged_process = [&](const std::string& request, auto callback) {
    std::cout << "→ " << request << std::endl;
    server.process(request, [callback](const std::string& response) {
        std::cout << "← " << response << std::endl;
        callback(response);
    });
};
```

## 8. 设计权衡与取舍

### 8.1 Header-only vs 编译分离

| | Header-only | 编译分离 |
|--|-------------|----------|
| **集成难度** | ✅ 零配置 | ❌ 需要编译链接 |
| **编译时间** | ❌ 每个 TU 都实例化模板 | ✅ 只编译一次 |
| **代码可读性** | ❌ 实现暴露在头文件中 | ✅ 接口与实现分离 |

选择 header-only，因为：
- 库代码量小（< 500 行），编译时间影响可忽略
- 大量模板代码本身无法编译分离
- 目标用户场景：嵌入到已有项目，零配置最重要

### 8.2 `std::variant` Id vs `json` Id

| | `variant<nullptr_t, int64_t, string>` | `json` |
|--|---------------------------------------|--------|
| **类型安全** | ✅ 编译期检查 | ❌ 运行时 |
| **存储效率** | ✅ 紧凑 | ❌ json 对象开销 |
| **使用便利** | ❌ 需要 `visit` | ✅ 直接操作 |

选择 `variant`，因为 id 是协议核心类型，类型安全更重要。通过 `id_to_json()` / `id_from_json()` 封装转换细节。

### 8.3 unordered_map vs 有序 map

方法路由使用 `std::unordered_map<string, HandlerPtr>`：

- 查找复杂度 O(1) 平均，适合高频请求分发
- 不需要有序遍历
- 方法名通常较短，hash 性能好

### 8.4 shared_ptr<Handler> vs unique_ptr<Handler>

Handler 使用 `shared_ptr` 管理：

- Batch 场景下多个回调 lambda 可能共享同一个 handler 引用
- `unordered_map` 存储 `shared_ptr` 支持值拷贝（虽然实际不需要）
- 性能差异在此场景可忽略

## 9. 测试策略

### 9.1 测试维度

测试按两个正交维度组织，形成 2×2 矩阵 + 协议层：

```
              位置参数 (array)    命名参数 (named)
同步 handler   test_sync_array    test_sync_named
异步 handler   test_async_array   test_async_named

协议级: test_protocol
```

### 9.2 测试覆盖

| 类别 | 用例数 | 覆盖内容 |
|------|--------|----------|
| 同步 + 数组 | 16 | 基本调用、多种返回类型、void、自定义类型、错误场景 |
| 同步 + 命名 | 14 | 命名参数、参数顺序无关、fallback 到数组、缺失参数 |
| 异步 + 数组 | 24 | 立即回调、线程回调、并发请求、异步 batch |
| 异步 + 命名 | 23 | 同上 + 命名参数变体 |
| 协议级 | 16 | parse error、invalid request、method not found、batch 边界 |
| **合计** | **93** | |

### 9.3 测试框架

自研轻量级测试框架（`test_helper.hpp`），核心工具：

- `send_expect(server, request, expected)` — 发送请求并断言响应
- `send_expect_empty(server, request)` — 发送 notification 并断言无响应
- `TestResults` — 收集并汇总测试结果

不依赖外部测试框架（如 Catch2、Google Test），保持零依赖的特性。

## 10. 总结

jsonrpc++ 的设计核心是**分层、类型安全、传输无关**。通过三层回调类型精确区分了 void/null/notification 三种语义，通过 `function_traits` 实现了编译期参数推导的零摩擦注册体验，通过只暴露一个异步 `process()` 入口实现了传输层的完全解耦。

整体设计优先考虑**正确性**（完整协议合规）和**易用性**（header-only、自动类型推导），在此前提下保持实现的简洁（< 500 行核心代码）。
