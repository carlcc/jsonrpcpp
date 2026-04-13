#pragma once

#include <functional>
#include <tuple>
#include <type_traits>

namespace jsonrpc {
namespace detail {

// Primary template (undefined)
template <typename F>
struct function_traits : function_traits<decltype(&std::decay_t<F>::operator())> {};

// Function type
template <typename R, typename... Args>
struct function_traits<R(Args...)> {
    using return_type = R;
    using argument_tuple = std::tuple<std::decay_t<Args>...>;
    static constexpr std::size_t arity = sizeof...(Args);

    template <std::size_t N>
    using argument_type = std::tuple_element_t<N, argument_tuple>;
};

// Function pointer
template <typename R, typename... Args>
struct function_traits<R(*)(Args...)> : function_traits<R(Args...)> {};

// Function reference
template <typename R, typename... Args>
struct function_traits<R(&)(Args...)> : function_traits<R(Args...)> {};

// Member function pointer (const and non-const)
template <typename C, typename R, typename... Args>
struct function_traits<R(C::*)(Args...)> : function_traits<R(Args...)> {};

template <typename C, typename R, typename... Args>
struct function_traits<R(C::*)(Args...) const> : function_traits<R(Args...)> {};

// noexcept variants
template <typename R, typename... Args>
struct function_traits<R(Args...) noexcept> : function_traits<R(Args...)> {};

template <typename R, typename... Args>
struct function_traits<R(*)(Args...) noexcept> : function_traits<R(Args...)> {};

template <typename C, typename R, typename... Args>
struct function_traits<R(C::*)(Args...) noexcept> : function_traits<R(Args...)> {};

template <typename C, typename R, typename... Args>
struct function_traits<R(C::*)(Args...) const noexcept> : function_traits<R(Args...)> {};

// std::function
template <typename R, typename... Args>
struct function_traits<std::function<R(Args...)>> : function_traits<R(Args...)> {};

} // namespace detail
} // namespace jsonrpc
