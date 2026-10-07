#pragma once

#include <concepts>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "Number.hpp"
#include "absl/container/flat_hash_map.h"

class JsonValue
{
public:
    using Array = std::vector<JsonValue>;
    using Object = absl::flat_hash_map<std::string, JsonValue>;
    using Storage = std::variant<std::monostate, bool, Number, std::string, Array, Object>;

    Storage value_;

public:
    JsonValue() = default;
    JsonValue(std::nullptr_t) noexcept {}

    JsonValue(const char* value) : value_(value ? std::string{value} : throw std::invalid_argument{"Null JSON string"})
    {
    }

    template <typename T>
        requires(!std::same_as<std::remove_cvref_t<T>, JsonValue> && std::constructible_from<Storage, T&&>)
    JsonValue(T&& value) : value_(std::forward<T>(value))
    {
    }

    [[nodiscard]] bool is_null() const noexcept { return std::holds_alternative<std::monostate>(value_); }

    JsonValue& operator[](std::string_view key)
    {
        if (is_null())
            value_.emplace<Object>();
        return std::get<Object>(value_)[key];
    }

    const JsonValue& operator[](std::string_view key) const { return std::get<Object>(value_).at(key); }

    JsonValue& operator[](std::size_t index)
    {
        if (is_null())
            value_.emplace<Array>();
        auto& array = std::get<Array>(value_);
        if (index >= array.max_size())
            throw std::out_of_range{"JSON array index out of range"};
        if (index >= array.size())
            array.resize(index + 1);
        return array[index];
    }

    const JsonValue& operator[](std::size_t index) const { return std::get<Array>(value_).at(index); }

    template <typename T>
    T& get()
    {
        return std::get<T>(value_);
    }

    template <typename T>
    const T& get() const
    {
        return std::get<T>(value_);
    }
};
