#pragma once

#include "errors.hpp"
#include "function_traits.hpp"
#include "types.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace jsonrpc {

// Internal callback type for handler results.
// - std::nullopt means the handler is a void function (no meaningful return value).
//   The caller can use this to distinguish "void return" from "returned null/empty string".
// - json(nullptr) means the function explicitly returned null.
// - json("") means the function explicitly returned an empty string.
using HandlerResult = std::optional<json>;
using InternalCallback = std::function<void(HandlerResult)>;

// User-facing callback type for async handlers.
// Users call this with a json value to deliver the result.
// This keeps the user API simple: cb(42), cb("hello"), cb(nullptr).
using ResponseCallback = std::function<void(json)>;

// Type-erased handler interface
// All handlers expose an async invoke interface.
// For sync-registered functions, the callback is called immediately.
struct Handler {
    virtual ~Handler() = default;

    // Unified async invoke: always delivers result via InternalCallback
    virtual void invoke(const json& params, InternalCallback cb) = 0;
};

using HandlerPtr = std::shared_ptr<Handler>;

namespace detail {

// ---- Helpers to extract args from JSON params ----

// Extract positional args from a JSON array
template <typename Tuple, std::size_t... Is>
Tuple extract_args_array(const json& params, std::index_sequence<Is...>) {
    if (!params.is_array()) {
        throw JsonRpcError(error_code::invalid_params, "Expected array params");
    }
    if (params.size() < sizeof...(Is)) {
        throw JsonRpcError(error_code::invalid_params,
            "Expected " + std::to_string(sizeof...(Is)) +
            " params, got " + std::to_string(params.size()));
    }
    return Tuple{params[Is].template get<std::tuple_element_t<Is, Tuple>>()...};
}

// Extract named args from a JSON object using parameter names
template <typename Tuple, std::size_t... Is>
Tuple extract_args_named(const json& params,
                         const std::vector<std::string>& names,
                         std::index_sequence<Is...>) {
    if (!params.is_object()) {
        throw JsonRpcError(error_code::invalid_params, "Expected object params for named arguments");
    }
    return Tuple{params.at(names[Is]).template get<std::tuple_element_t<Is, Tuple>>()...};
}

// ---- SyncHandler: wraps a normal function (returns a value) ----

template <typename F>
class SyncHandler : public Handler {
public:
    using traits = function_traits<F>;
    using return_type = typename traits::return_type;
    using arg_tuple = typename traits::argument_tuple;
    static constexpr std::size_t arity = traits::arity;

    explicit SyncHandler(F func) : func_(std::move(func)) {}

    SyncHandler(F func, std::vector<std::string> param_names)
        : func_(std::move(func))
        , param_names_(std::move(param_names))
    {}

    void invoke(const json& params, InternalCallback cb) override {
        try {
            HandlerResult result = invoke_impl(params);
            cb(std::move(result));
        } catch (const JsonRpcError&) {
            throw; // re-throw JsonRpcError for upper layers to handle
        } catch (const nlohmann::json::exception& e) {
            throw JsonRpcError(error_code::invalid_params, e.what());
        } catch (const std::exception& e) {
            throw JsonRpcError(error_code::internal_error, e.what());
        }
    }

private:
    HandlerResult invoke_impl(const json& params) {
        auto args = extract_args(params);
        if constexpr (std::is_void_v<return_type>) {
            std::apply(func_, std::move(args));
            return std::nullopt;  // void return → nullopt (distinct from json(nullptr))
        } else {
            return json(std::apply(func_, std::move(args)));
        }
    }

    arg_tuple extract_args(const json& params) {
        if constexpr (arity == 0) {
            return {};
        } else {
            auto seq = std::make_index_sequence<arity>{};
            if (!param_names_.empty() && params.is_object()) {
                return extract_args_named<arg_tuple>(params, param_names_, seq);
            } else {
                // For array params, or if no param names provided
                return extract_args_array<arg_tuple>(params, seq);
            }
        }
    }

    F func_;
    std::vector<std::string> param_names_;
};

// ---- AsyncHandler: wraps a function whose last arg is ResponseCallback ----
// The user function signature is: void func(arg1, arg2, ..., ResponseCallback)
// We strip the last argument (the callback) from type deduction.

// Helper: remove last element from a tuple type
template <typename Tuple>
struct remove_last;

template <typename T>
struct remove_last<std::tuple<T>> {
    using type = std::tuple<>;
};

template <typename Head, typename... Tail>
struct remove_last<std::tuple<Head, Tail...>> {
    using type = decltype(std::tuple_cat(
        std::declval<std::tuple<Head>>(),
        std::declval<typename remove_last<std::tuple<Tail...>>::type>()
    ));
};

template <>
struct remove_last<std::tuple<>> {
    using type = std::tuple<>;
};

template <typename Tuple>
using remove_last_t = typename remove_last<Tuple>::type;

template <typename F>
class AsyncHandler : public Handler {
public:
    using traits = function_traits<F>;
    using full_arg_tuple = typename traits::argument_tuple;
    // The actual RPC params are all args except the last (ResponseCallback)
    using arg_tuple = remove_last_t<full_arg_tuple>;
    static constexpr std::size_t full_arity = traits::arity;
    static constexpr std::size_t arity = full_arity > 0 ? full_arity - 1 : 0;

    explicit AsyncHandler(F func) : func_(std::move(func)) {}

    AsyncHandler(F func, std::vector<std::string> param_names)
        : func_(std::move(func))
        , param_names_(std::move(param_names))
    {}

    void invoke(const json& params, InternalCallback cb) override {
        try {
            auto args = extract_args(params);
            // Wrap InternalCallback into a user-friendly ResponseCallback.
            // User calls cb(json_value), we forward it as HandlerResult(json_value).
            ResponseCallback user_cb = [cb = std::move(cb)](json result) {
                cb(HandlerResult(std::move(result)));
            };
            // Call the user function with extracted args + the user-facing callback
            auto full_args = std::tuple_cat(std::move(args), std::make_tuple(std::move(user_cb)));
            std::apply(func_, std::move(full_args));
        } catch (const JsonRpcError&) {
            throw;
        } catch (const nlohmann::json::exception& e) {
            throw JsonRpcError(error_code::invalid_params, e.what());
        } catch (const std::exception& e) {
            throw JsonRpcError(error_code::internal_error, e.what());
        }
    }

private:
    arg_tuple extract_args(const json& params) {
        if constexpr (arity == 0) {
            return {};
        } else {
            auto seq = std::make_index_sequence<arity>{};
            if (!param_names_.empty() && params.is_object()) {
                return extract_args_named<arg_tuple>(params, param_names_, seq);
            } else {
                return extract_args_array<arg_tuple>(params, seq);
            }
        }
    }

    F func_;
    std::vector<std::string> param_names_;
};

} // namespace detail

// ---- Factory functions ----

template <typename F>
HandlerPtr make_handler(F&& func) {
    return std::make_shared<detail::SyncHandler<std::decay_t<F>>>(std::forward<F>(func));
}

template <typename F>
HandlerPtr make_handler(F&& func, std::vector<std::string> param_names) {
    return std::make_shared<detail::SyncHandler<std::decay_t<F>>>(
        std::forward<F>(func), std::move(param_names));
}

template <typename F>
HandlerPtr make_async_handler(F&& func) {
    return std::make_shared<detail::AsyncHandler<std::decay_t<F>>>(std::forward<F>(func));
}

template <typename F>
HandlerPtr make_async_handler(F&& func, std::vector<std::string> param_names) {
    return std::make_shared<detail::AsyncHandler<std::decay_t<F>>>(
        std::forward<F>(func), std::move(param_names));
}

} // namespace jsonrpc
