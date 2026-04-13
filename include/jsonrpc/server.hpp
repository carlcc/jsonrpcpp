#pragma once

#include "dispatcher.hpp"
#include "errors.hpp"
#include "types.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace jsonrpc {

class Server {
public:
    // ---- Method registration ----

    template <typename F>
    void bind(const std::string& method, F&& func) {
        dispatcher_.bind(method, std::forward<F>(func));
    }

    template <typename F>
    void bind(const std::string& method, F&& func,
              std::vector<std::string> param_names) {
        dispatcher_.bind(method, std::forward<F>(func), std::move(param_names));
    }

    template <typename F>
    void bind_async(const std::string& method, F&& func) {
        dispatcher_.bind_async(method, std::forward<F>(func));
    }

    template <typename F>
    void bind_async(const std::string& method, F&& func,
                    std::vector<std::string> param_names) {
        dispatcher_.bind_async(method, std::forward<F>(func), std::move(param_names));
    }

    // ---- Unified async processing interface ----

    using ProcessCallback = std::function<void(std::string)>;

    void process(const std::string& json_request, ProcessCallback callback) {
        // Parse JSON
        json parsed;
        try {
            parsed = json::parse(json_request);
        } catch (const json::parse_error& e) {
            auto resp = make_error_response(make_parse_error(e.what()));
            callback(resp.dump());
            return;
        }

        // Batch request (JSON array)
        if (parsed.is_array()) {
            process_batch(parsed, std::move(callback));
            return;
        }

        // Single request
        if (parsed.is_object()) {
            process_single(parsed, std::move(callback));
            return;
        }

        // Neither array nor object: invalid request
        auto resp = make_error_response(make_invalid_request());
        callback(resp.dump());
    }

private:
    void process_single(const json& j, ProcessCallback callback) {
        // Parse the request structure
        Request req;
        try {
            req = parse_request(j);
        } catch (const std::exception& e) {
            auto resp = make_error_response(make_invalid_request(e.what()));
            callback(resp.dump());
            return;
        }

        dispatcher_.dispatch(req, [cb = std::move(callback)](std::optional<json> response) {
            if (!response.has_value()) {
                // Notification: no response to send
                cb("");
                return;
            }
            cb(response->dump());
        });
    }

    void process_batch(const json& batch, ProcessCallback callback) {
        if (batch.empty()) {
            // Empty array is an invalid request per spec
            auto resp = make_error_response(make_invalid_request("Empty batch"));
            callback(resp.dump());
            return;
        }

        // We need to collect all responses. Some may be async, so we use
        // a shared state to track completion.
        struct BatchState {
            std::mutex mutex;
            std::vector<json> responses;
            std::size_t total;
            std::atomic<std::size_t> completed{0};
            ProcessCallback callback;

            BatchState(std::size_t n, ProcessCallback cb)
                : total(n), callback(std::move(cb)) {
                responses.reserve(n);
            }

            void add_response(std::optional<json> resp) {
                bool done = false;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (resp.has_value()) {
                        responses.push_back(std::move(*resp));
                    }
                    // else: notification, no response to collect
                    if (completed.fetch_add(1) + 1 == total) {
                        done = true;
                    }
                }
                if (done) {
                    if (responses.empty()) {
                        // All were notifications: no response
                        callback("");
                    } else {
                        callback(json(responses).dump());
                    }
                }
            }
        };

        auto state = std::make_shared<BatchState>(batch.size(), std::move(callback));

        for (const auto& item : batch) {
            if (!item.is_object()) {
                state->add_response(make_error_response(make_invalid_request()));
                continue;
            }

            Request req;
            try {
                req = parse_request(item);
            } catch (const std::exception& e) {
                state->add_response(make_error_response(make_invalid_request(e.what())));
                continue;
            }

            dispatcher_.dispatch(req, [state](std::optional<json> response) {
                state->add_response(std::move(response));
            });
        }
    }

    Dispatcher dispatcher_;
};

} // namespace jsonrpc
