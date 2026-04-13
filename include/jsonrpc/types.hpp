#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>

namespace jsonrpc {

using json = nlohmann::json;

// JSON-RPC 2.0 id can be string, integer, or null
using Id = std::variant<std::nullptr_t, int64_t, std::string>;

inline json id_to_json(const Id& id) {
    return std::visit([](auto&& val) -> json {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return nullptr;
        } else {
            return val;
        }
    }, id);
}

inline Id id_from_json(const json& j) {
    if (j.is_null()) {
        return nullptr;
    } else if (j.is_number_integer()) {
        return j.get<int64_t>();
    } else if (j.is_string()) {
        return j.get<std::string>();
    }
    return nullptr;
}

// JSON-RPC 2.0 Error object
struct Error {
    int code = 0;
    std::string message;
    json data = nullptr;  // optional

    json to_json() const {
        json j;
        j["code"] = code;
        j["message"] = message;
        if (!data.is_null()) {
            j["data"] = data;
        }
        return j;
    }
};

// JSON-RPC 2.0 Request (may be a notification if id is absent)
struct Request {
    std::string method;
    json params = nullptr;   // array or object, optional
    std::optional<Id> id;    // absent for notifications

    bool is_notification() const {
        return !id.has_value();
    }
};

// Parse a single request from JSON. Throws on invalid format.
inline Request parse_request(const json& j) {
    Request req;

    // Must be an object
    if (!j.is_object()) {
        throw std::invalid_argument("Request must be a JSON object");
    }

    // Must have "jsonrpc": "2.0"
    if (!j.contains("jsonrpc") || j["jsonrpc"] != "2.0") {
        throw std::invalid_argument("Missing or invalid 'jsonrpc' field");
    }

    // Must have "method" as string
    if (!j.contains("method") || !j["method"].is_string()) {
        throw std::invalid_argument("Missing or invalid 'method' field");
    }
    req.method = j["method"].get<std::string>();

    // Optional "params" (must be array or object if present)
    if (j.contains("params")) {
        if (!j["params"].is_array() && !j["params"].is_object()) {
            throw std::invalid_argument("'params' must be an array or object");
        }
        req.params = j["params"];
    }

    // Optional "id" (if present, it's a request; if absent, it's a notification)
    if (j.contains("id")) {
        req.id = id_from_json(j["id"]);
    }

    return req;
}

// Build a success response JSON
inline json make_response(const Id& id, const json& result) {
    return {
        {"jsonrpc", "2.0"},
        {"result", result},
        {"id", id_to_json(id)}
    };
}

// Build an error response JSON
inline json make_error_response(const Id& id, const Error& error) {
    return {
        {"jsonrpc", "2.0"},
        {"error", error.to_json()},
        {"id", id_to_json(id)}
    };
}

// Build an error response without a known id (e.g. parse error)
inline json make_error_response(const Error& error) {
    return {
        {"jsonrpc", "2.0"},
        {"error", error.to_json()},
        {"id", nullptr}
    };
}

} // namespace jsonrpc
