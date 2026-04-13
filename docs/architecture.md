# jsonrpc++ 架构文档

> 版本：1.0 | 最后更新：2026-04-13

## 1. 总体架构

jsonrpc++ 采用**分层架构**，自底向上由五个层次组成。每一层职责单一，只依赖下方层级，不反向依赖。

```
┌──────────────────────────────────────────────────────────────┐
│                        应用层 (Application)                   │
│                  用户集成代码 (stdio / TCP / WebSocket …)      │
│                  调用 Server::process(string, callback)       │
├──────────────────────────────────────────────────────────────┤
│                        Server 层                              │
│  server.hpp                                                   │
│  · JSON 字符串解析                                             │
│  · 单请求 / Batch 分流                                         │
│  · 协议级错误处理 (parse error / invalid request)              │
├──────────────────────────────────────────────────────────────┤
│                      Dispatcher 层                            │
│  dispatcher.hpp                                               │
│  · 方法路由 (method → Handler 映射)                            │
│  · Notification 语义 (无 id → 静默执行，不回传响应)             │
│  · 异常到 Error Response 的转换                                │
├──────────────────────────────────────────────────────────────┤
│                       Handler 层                              │
│  handler.hpp                                                  │
│  · SyncHandler<F>：同步函数包装                                │
│  · AsyncHandler<F>：异步函数包装 (最后一个参数为 callback)      │
│  · JSON 参数提取 (位置参数 / 命名参数)                          │
│  · 返回值语义 (void → nullopt / value → json)                  │
├──────────────────────────────────────────────────────────────┤
│                      基础设施层                                │
│  types.hpp          核心类型定义                               │
│  errors.hpp         标准错误码与异常                            │
│  function_traits.hpp 编译期函数签名推导                         │
└──────────────────────────────────────────────────────────────┘
```

### 1.1 依赖关系图

```
jsonrpc.hpp (总入口 — 聚合所有头文件)
    │
    ├── server.hpp
    │       ├── dispatcher.hpp
    │       │       ├── handler.hpp
    │       │       │       ├── function_traits.hpp
    │       │       │       ├── errors.hpp
    │       │       │       │       └── types.hpp
    │       │       │       └── types.hpp
    │       │       ├── errors.hpp
    │       │       └── types.hpp
    │       ├── errors.hpp
    │       └── types.hpp
    ├── dispatcher.hpp
    ├── handler.hpp
    ├── function_traits.hpp
    ├── errors.hpp
    └── types.hpp
```

所有头文件使用 `#pragma once` 保护，可以安全地多次包含。用户只需 `#include <jsonrpc/jsonrpc.hpp>` 即可引入全部功能。

## 2. 组件详解

### 2.1 types.hpp — 核心类型

定义 JSON-RPC 2.0 协议所需的基本类型：

| 类型/函数 | 说明 |
|-----------|------|
| `Id = std::variant<std::nullptr_t, int64_t, std::string>` | 请求 ID，支持 null / 整数 / 字符串 |
| `Error` | 错误对象 `{code, message, data}` |
| `Request` | 解析后的请求 `{method, params, id?}` |
| `id_to_json()` / `id_from_json()` | Id 与 JSON 的双向转换 |
| `parse_request()` | 从 JSON 对象解析 Request（含完整校验） |
| `make_response()` | 构造成功响应 |
| `make_error_response()` | 构造错误响应（有 id / 无 id 两个重载） |

**设计要点**：
- `Request::id` 使用 `std::optional<Id>`，`has_value()` 区分 Request 和 Notification
- `Id` 使用 `std::variant` 而非 `json`，提供类型安全的 id 表示

### 2.2 errors.hpp — 错误系统

```
error_code 命名空间     → constexpr int 常量
error_message 命名空间  → constexpr const char* 常量
JsonRpcError 异常类     → 继承 std::runtime_error，携带 Error 对象
make_xxx_error() 工厂   → 快速构造标准 Error
```

标准错误码：

| 常量 | 值 | 含义 |
|------|----|------|
| `parse_error` | -32700 | JSON 解析失败 |
| `invalid_request` | -32600 | 非法的 JSON-RPC 请求结构 |
| `method_not_found` | -32601 | 请求的方法未注册 |
| `invalid_params` | -32602 | 参数类型错误或数量不匹配 |
| `internal_error` | -32603 | Handler 内部异常 |

`JsonRpcError` 既可以在 handler 中抛出以返回自定义错误，也被框架内部用于包装参数解析异常。

### 2.3 function_traits.hpp — 编译期类型推导

```cpp
template <typename F>
struct function_traits {
    using return_type    = ...;    // 返回值类型
    using argument_tuple = ...;    // std::tuple<Args...>
    static constexpr std::size_t arity = N;  // 参数个数
};
```

支持的函数形式（共 10 种特化）：

| 形式 | 示例 |
|------|------|
| Lambda / 可调用对象 | `[](int a) { return a; }` |
| 普通函数类型 | `int(int, int)` |
| 函数指针 | `int(*)(int)` |
| 函数引用 | `int(&)(int)` |
| 成员函数指针 | `int(Foo::*)(int)` |
| const 成员函数指针 | `int(Foo::*)(int) const` |
| noexcept 变体（以上各种） | `int(int) noexcept` |
| `std::function` | `std::function<int(int)>` |

**核心机制**：主模板递归到 `operator()` 的类型，所有特化最终归约到 `R(Args...)` 这一基础形式。

### 2.4 handler.hpp — Handler 抽象与实现

#### 2.4.1 类型体系

```
Handler (抽象基类)
  │
  ├── SyncHandler<F>    —— 包装同步函数
  │     invoke() → 调用函数 → 立即回调 InternalCallback
  │
  └── AsyncHandler<F>   —— 包装异步函数
        invoke() → 调用函数（传入 ResponseCallback）→ 用户择机回调
```

#### 2.4.2 三层回调类型

这是本库最核心的设计之一。三种 callback 类型服务于不同的抽象层级：

```
用户层                    内部层                      分发层
ResponseCallback         InternalCallback            DispatchCallback
void(json)          →    void(HandlerResult)     →   void(optional<json>)
                          HandlerResult =              nullopt = notification
                          optional<json>                json = 完整响应对象
                          nullopt = void 返回
                          json(nullptr) = 显式 null
```

**为什么需要三层？**

| 问题 | 解决方案 |
|------|----------|
| 用户 async handler 不应关心 void/null 的区别 | `ResponseCallback` 只接受 `json`，简洁 |
| Dispatcher 需区分 "handler 返回 void" 和 "handler 返回 null" | `HandlerResult = optional<json>`，`nullopt` ≠ `json(nullptr)` |
| Server 需区分 "notification 无需回传" 和 "有响应需回传" | `DispatchCallback` 用 `optional<json>` 表示 |

#### 2.4.3 参数提取

两个核心模板函数：

- `extract_args_array<Tuple>(params, index_sequence)` — 从 JSON 数组按位置提取
- `extract_args_named<Tuple>(params, names, index_sequence)` — 从 JSON 对象按名称提取

提取策略在运行时动态选择：如果注册了命名参数 **且** 请求使用了对象参数，则按名称提取；否则回退到数组提取。

#### 2.4.4 AsyncHandler 的参数剥离

`AsyncHandler<F>` 需要将用户函数签名的最后一个参数（`ResponseCallback`）从 RPC 参数推导中剥离：

```cpp
// 用户函数: void(int, string, ResponseCallback)
// full_arg_tuple = tuple<int, string, ResponseCallback>
// arg_tuple      = tuple<int, string>     ← remove_last_t
// arity          = 2                       ← 用于 JSON 参数提取
```

`remove_last<Tuple>` 使用递归模板在编译期移除 tuple 的最后一个元素。

### 2.5 dispatcher.hpp — 方法分发

```cpp
class Dispatcher {
    unordered_map<string, HandlerPtr> handlers_;

    void dispatch(const Request& req, DispatchCallback cb);
};
```

分发逻辑：

```
dispatch(req, cb)
  │
  ├── 查找 handlers_[req.method]
  │     未找到 → notification? → cb(nullopt)
  │              request?     → cb(method_not_found 错误响应)
  │
  ├── 是 Notification?
  │     → 执行 handler (忽略结果和异常) → cb(nullopt)
  │
  └── 是 Request?
        → handler->invoke(params, internal_cb)
           internal_cb:
             result.has_value()  → cb(make_response(id, *result))
             result == nullopt   → cb(make_response(id, null))   // void → null
        → catch JsonRpcError    → cb(error_response)
        → catch std::exception  → cb(internal_error_response)
```

### 2.6 server.hpp — Server 入口

```cpp
class Server {
    Dispatcher dispatcher_;

    void process(const string& json_request, ProcessCallback callback);
};
```

处理流程：

```
process(json_string, callback)
  │
  ├── json::parse() 失败 → callback(parse_error 响应)
  │
  ├── 解析结果是 object → process_single()
  │     → parse_request() 失败 → callback(invalid_request 响应)
  │     → dispatcher_.dispatch(req, ...)
  │         → notification → callback("")     // 空字符串 = 无需回传
  │         → response     → callback(response.dump())
  │
  ├── 解析结果是 array → process_batch()
  │     → 空数组 → callback(invalid_request 响应)
  │     → 创建 BatchState (shared_ptr)
  │         → 对每个元素: parse_request → dispatch
  │         → 每个完成时 add_response()
  │         → atomic 计数，全部完成后:
  │             全是 notification → callback("")
  │             有响应          → callback(json(responses).dump())
  │
  └── 既非 object 也非 array → callback(invalid_request 响应)
```

## 3. 数据流

### 3.1 单请求处理流

```
应用层                    Server              Dispatcher           Handler
  │                        │                     │                    │
  │  process(json_str, cb) │                     │                    │
  │───────────────────────>│                     │                    │
  │                        │ json::parse()       │                    │
  │                        │ parse_request()     │                    │
  │                        │                     │                    │
  │                        │  dispatch(req, cb)  │                    │
  │                        │────────────────────>│                    │
  │                        │                     │ invoke(params, cb) │
  │                        │                     │───────────────────>│
  │                        │                     │                    │
  │                        │                     │  cb(HandlerResult) │
  │                        │                     │<───────────────────│
  │                        │                     │                    │
  │                        │  cb(optional<json>) │                    │
  │                        │<────────────────────│                    │
  │                        │                     │                    │
  │   callback(string)     │                     │                    │
  │<───────────────────────│                     │                    │
```

### 3.2 Batch 请求处理流

```
应用层          Server                Dispatcher        Handler(s)
  │              │                       │                  │
  │  process()   │                       │                  │
  │─────────────>│                       │                  │
  │              │ 创建 BatchState        │                  │
  │              │ (shared_ptr + atomic)  │                  │
  │              │                       │                  │
  │              │──── dispatch(req[0]) ─>│──── invoke() ──>│
  │              │──── dispatch(req[1]) ─>│──── invoke() ──>│
  │              │──── dispatch(req[2]) ─>│──── invoke() ──>│
  │              │      ...              │       ...        │
  │              │                       │                  │
  │              │       BatchState::add_response() × N     │
  │              │       (线程安全，atomic 计数)              │
  │              │                       │                  │
  │              │       最后一个完成:     │                  │
  │              │       callback(json(responses).dump())   │
  │<─────────────│                       │                  │
```

### 3.3 异步 Handler 数据流

```
应用层         Server        Dispatcher       AsyncHandler       用户函数
  │              │               │                 │                 │
  │  process()   │               │                 │                 │
  │─────────────>│               │                 │                 │
  │              │  dispatch()   │                 │                 │
  │              │──────────────>│                 │                 │
  │              │               │  invoke()       │                 │
  │              │               │────────────────>│                 │
  │              │               │                 │  提取参数        │
  │              │               │                 │  包装 callback   │
  │              │               │                 │  func(args, cb) │
  │              │               │                 │────────────────>│
  │              │               │                 │                 │
  │              │  (此时控制流已返回，用户函数可在任意线程/时机完成)     │
  │              │               │                 │                 │
  │              │               │                 │   cb(result)    │
  │              │               │                 │<────────────────│
  │              │               │ DispatchCallback │                 │
  │              │               │<────────────────│                 │
  │              │  ProcessCb    │                 │                 │
  │              │<──────────────│                 │                 │
  │  callback()  │               │                 │                 │
  │<─────────────│               │                 │                 │
```

## 4. 线程安全模型

### 4.1 同步模式

在同步模式下，所有回调在调用 `process()` 的线程中同步完成，不涉及线程安全问题。

### 4.2 异步模式

- **Handler 注册**：`bind()` / `bind_async()` 不是线程安全的，应在处理请求前完成所有注册
- **请求处理**：单个 `Server` 实例的 `process()` 可以从多个线程并发调用（每次调用使用独立的状态）
- **异步回调**：`ResponseCallback` 可在任意线程调用，回调链上的 `ProcessCallback` 会在该线程被调用
- **Batch 并发**：`BatchState` 使用 `std::mutex` + `std::atomic` 保证多个异步 handler 并发完成时的安全性

### 4.3 生命周期管理

```
Server         → 值语义，内嵌 Dispatcher
Dispatcher     → 值语义，内嵌 handlers_ map
Handler        → shared_ptr 管理
BatchState     → shared_ptr 管理（被所有 batch item 的回调共享）
ResponseCallback → 通过 lambda 捕获 shared_ptr，延长 BatchState 生命周期
```

## 5. 编译期与运行时的分工

| 阶段 | 工作 | 实现机制 |
|------|------|----------|
| **编译期** | 推导函数签名 (返回类型、参数类型、参数个数) | `function_traits` 模板特化 |
| **编译期** | 生成参数提取代码 (针对具体类型的 `json::get<T>()` 调用) | `extract_args_array/named` + `index_sequence` |
| **编译期** | 选择 void/非 void 处理路径 | `if constexpr (is_void_v<return_type>)` |
| **编译期** | 选择 0 参数/多参数提取路径 | `if constexpr (arity == 0)` |
| **编译期** | 剥离 AsyncHandler 的 callback 参数 | `remove_last_t` 递归模板 |
| **运行时** | JSON 解析、请求路由、参数值提取 | nlohmann/json、unordered_map 查找 |
| **运行时** | 命名参数 vs 数组参数的选择 | `!param_names_.empty() && params.is_object()` |
| **运行时** | Batch 完成追踪 | `atomic<size_t>` 计数 |

## 6. 错误处理策略

```
Handler 层:
  nlohmann::json::exception → 包装为 JsonRpcError(invalid_params)
  std::exception            → 包装为 JsonRpcError(internal_error)
  JsonRpcError              → 直接上抛

Dispatcher 层:
  方法未找到                 → make_method_not_found() 错误响应
  JsonRpcError              → 用 e.error() 构造错误响应
  std::exception            → make_internal_error() 错误响应
  Notification 的异常       → 静默吞掉（规范要求不回传）

Server 层:
  JSON parse 失败            → parse_error 错误响应
  Request 结构无效           → invalid_request 错误响应
  空 batch                   → invalid_request 错误响应
  非 object 也非 array       → invalid_request 错误响应
```

每一层只处理该层能处理的错误，其余向上传递。保证任何输入都不会导致未处理异常泄露到用户层。

## 7. 外部依赖

| 依赖 | 版本要求 | 用途 |
|------|----------|------|
| [nlohmann/json](https://github.com/nlohmann/json) | ≥ 3.9.0 | JSON 解析、序列化、类型转换 |
| C++17 标准库 | — | `variant`, `optional`, `tuple`, `shared_ptr`, `atomic`, `mutex`, `function` |

无其他第三方依赖。
