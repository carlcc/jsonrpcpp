#pragma once

#include "types.hpp"
#include <stdexcept>
#include <string>

namespace jsonrpc {

// JSON-RPC 2.0 standard error codes
namespace error_code {
    constexpr int parse_error      = -32700;
    constexpr int invalid_request  = -32600;
    constexpr int method_not_found = -32601;
    constexpr int invalid_params   = -32602;
    constexpr int internal_error   = -32603;
}

// Default error messages
namespace error_message {
    constexpr const char* parse_error      = "Parse error";
    constexpr const char* invalid_request  = "Invalid Request";
    constexpr const char* method_not_found = "Method not found";
    constexpr const char* invalid_params   = "Invalid params";
    constexpr const char* internal_error   = "Internal error";
}

// Exception class for JSON-RPC errors
class JsonRpcError : public std::runtime_error {
public:
    JsonRpcError(int code, const std::string& message, json data = nullptr)
        : std::runtime_error(message)
        , error_{code, message, std::move(data)}
    {}

    const Error& error() const noexcept { return error_; }
    int code() const noexcept { return error_.code; }

private:
    Error error_;
};

// Convenience factory functions for standard errors
inline Error make_parse_error(json data = nullptr) {
    return {error_code::parse_error, error_message::parse_error, std::move(data)};
}

inline Error make_invalid_request(json data = nullptr) {
    return {error_code::invalid_request, error_message::invalid_request, std::move(data)};
}

inline Error make_method_not_found(json data = nullptr) {
    return {error_code::method_not_found, error_message::method_not_found, std::move(data)};
}

inline Error make_invalid_params(json data = nullptr) {
    return {error_code::invalid_params, error_message::invalid_params, std::move(data)};
}

inline Error make_internal_error(json data = nullptr) {
    return {error_code::internal_error, error_message::internal_error, std::move(data)};
}

} // namespace jsonrpc
