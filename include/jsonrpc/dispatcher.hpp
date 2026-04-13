#pragma once

#include "errors.hpp"
#include "handler.hpp"
#include "types.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace jsonrpc {

// Callback for dispatch results.
// - std::nullopt means no response should be sent (notification).
// - A json value is the full response object to send.
using DispatchCallback = std::function<void(std::optional<json>)>;

class Dispatcher {
public:
    // Register a synchronous handler
    template <typename F>
    void bind(const std::string& method, F&& func) {
        handlers_[method] = make_handler(std::forward<F>(func));
    }

    // Register a synchronous handler with named parameters
    template <typename F>
    void bind(const std::string& method, F&& func,
              std::vector<std::string> param_names) {
        handlers_[method] = make_handler(std::forward<F>(func), std::move(param_names));
    }

    // Register an asynchronous handler
    template <typename F>
    void bind_async(const std::string& method, F&& func) {
        handlers_[method] = make_async_handler(std::forward<F>(func));
    }

    // Register an asynchronous handler with named parameters
    template <typename F>
    void bind_async(const std::string& method, F&& func,
                    std::vector<std::string> param_names) {
        handlers_[method] = make_async_handler(std::forward<F>(func), std::move(param_names));
    }

    // Dispatch a single parsed request.
    // Calls the callback with:
    //   - std::nullopt if it's a notification (no response to send)
    //   - A json response object otherwise
    void dispatch(const Request& req, DispatchCallback cb) {
        // Look up handler
        auto it = handlers_.find(req.method);
        if (it == handlers_.end()) {
            if (req.is_notification()) {
                // Notifications don't get responses, even for errors
                cb(std::nullopt);
                return;
            }
            cb(make_error_response(*req.id, make_method_not_found()));
            return;
        }

        auto& handler = it->second;

        if (req.is_notification()) {
            // Notification: execute but don't return any response
            try {
                handler->invoke(req.params, [](HandlerResult) {
                    // discard result for notifications
                });
            } catch (...) {
                // Notifications must not generate responses, even on error
            }
            cb(std::nullopt);
            return;
        }

        // Normal request with id
        Id id = *req.id;
        try {
            handler->invoke(req.params, [id, cb](HandlerResult result) {
                // void return (nullopt) → "result": null per JSON-RPC 2.0 spec
                json result_value = result.has_value() ? std::move(*result) : json(nullptr);
                cb(make_response(id, std::move(result_value)));
            });
        } catch (const JsonRpcError& e) {
            cb(make_error_response(id, e.error()));
        } catch (const std::exception& e) {
            cb(make_error_response(id, make_internal_error(e.what())));
        }
    }

private:
    std::unordered_map<std::string, HandlerPtr> handlers_;
};

} // namespace jsonrpc
