#pragma once

#include "absl/strings/str_cat.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

class Number
{
    template <typename T>
    static constexpr bool is_numeric_v = std::is_same_v<T, std::remove_cv_t<T>> &&
        ((std::is_integral_v<T> && !std::is_same_v<T, bool>) || std::is_floating_point_v<T>);

    std::string value_;

    static bool is_digit(char value) noexcept { return value >= '0' && value <= '9'; }

    static bool is_valid(std::string_view value) noexcept
    {
        std::size_t position = 0;
        if (position < value.size() && value[position] == '-')
            ++position;

        if (position == value.size())
            return false;
        if (value[position] == '0')
            ++position;
        else if (value[position] >= '1' && value[position] <= '9')
        {
            while (position < value.size() && is_digit(value[position]))
                ++position;
        }
        else
            return false;

        if (position < value.size() && value[position] == '.')
        {
            ++position;
            const auto begin = position;
            while (position < value.size() && is_digit(value[position]))
                ++position;
            if (position == begin)
                return false;
        }

        if (position < value.size() && (value[position] == 'e' || value[position] == 'E'))
        {
            ++position;
            if (position < value.size() && (value[position] == '+' || value[position] == '-'))
                ++position;
            const auto begin = position;
            while (position < value.size() && is_digit(value[position]))
                ++position;
            if (position == begin)
                return false;
        }

        return position == value.size();
    }

    template <typename T>
    static std::string format(T value)
    {
        if constexpr (std::is_integral_v<T>)
        {
            if constexpr (std::is_signed_v<T>)
                return absl::StrCat(static_cast<std::intmax_t>(value));
            else
                return absl::StrCat(static_cast<std::uintmax_t>(value));
        }
        else
        {
            if (!std::isfinite(value))
                throw std::invalid_argument{"Invalid JSON number"};
            if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>)
                return absl::StrCat(absl::HighPrecision(value));
            else
            {
                char buffer[128];
                const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
                if (result.ec != std::errc{})
                    throw std::runtime_error{"Number conversion failed"};
                return std::string(buffer, result.ptr);
            }
        }
    }

    template <typename T>
    T as_integer() const
    {
        const bool negative = value_.front() == '-';
        if constexpr (std::is_unsigned_v<T>)
        {
            if (negative)
                return T{0};
        }

        const T boundary = negative ? std::numeric_limits<T>::min() : std::numeric_limits<T>::max();
        const std::uintmax_t limit = negative ? std::uintmax_t{0} - static_cast<std::uintmax_t>(boundary)
                                              : static_cast<std::uintmax_t>(boundary);
        constexpr std::size_t max_digits = std::numeric_limits<T>::digits10 + 1;
        const auto exponent_position = value_.find_first_of("eE");
        const auto mantissa_end = exponent_position == std::string::npos ? value_.size() : exponent_position;
        const std::size_t begin = negative ? 1 : 0;
        std::string digits = value_.substr(begin, mantissa_end - begin);
        const auto point = digits.find('.');
        const auto integer_digits = point == std::string::npos ? digits.size() : point;
        if (point != std::string::npos)
            digits.erase(point, 1);

        const auto leading_zeros = digits.find_first_not_of('0');
        if (leading_zeros == std::string::npos)
            return T{0};

        std::size_t exponent = 0;
        bool negative_exponent = false;
        if (exponent_position != std::string::npos)
        {
            auto position = exponent_position + 1;
            negative_exponent = value_[position] == '-';
            if (value_[position] == '+' || value_[position] == '-')
                ++position;
            const auto parsed = std::from_chars(value_.data() + position, value_.data() + value_.size(), exponent);
            if (parsed.ec == std::errc::result_out_of_range)
                return negative_exponent ? T{0} : boundary;
        }

        // Shift the decimal point using digit counts, without rounding through a floating-point type.
        std::size_t result_digits = 0;
        if (negative_exponent)
        {
            if (integer_digits <= leading_zeros || exponent >= integer_digits - leading_zeros)
                return T{0};
            result_digits = integer_digits - leading_zeros - exponent;
        }
        else if (integer_digits >= leading_zeros)
        {
            result_digits = integer_digits - leading_zeros;
            if (exponent > max_digits || result_digits > max_digits - exponent)
                return boundary;
            result_digits += exponent;
        }
        else
        {
            const auto fractional_zeros = leading_zeros - integer_digits;
            if (exponent <= fractional_zeros)
                return T{0};
            result_digits = exponent - fractional_zeros;
        }

        if (result_digits > max_digits)
            return boundary;

        const auto significant = std::string_view(digits).substr(leading_zeros);
        std::uintmax_t magnitude = 0;
        for (std::size_t index = 0; index < result_digits; ++index)
        {
            const unsigned digit = index < significant.size() ? significant[index] - '0' : 0;
            if (magnitude > limit / 10 || (magnitude == limit / 10 && digit > limit % 10))
                return boundary;
            magnitude = magnitude * 10 + digit;
        }

        if (negative)
        {
            if (magnitude == limit)
                return boundary;
            return static_cast<T>(-static_cast<std::intmax_t>(magnitude));
        }
        return static_cast<T>(magnitude);
    }

public:
    Number(std::string value) : value_(std::move(value))
    {
        if (!is_valid(value_))
            throw std::invalid_argument{"Invalid JSON number"};
    }

    Number(const char* value) : Number(value ? std::string(value) : throw std::invalid_argument{"Invalid JSON number"})
    {
    }

    template <typename T>
        requires is_numeric_v<T>
    Number(T value) : Number(format(value))
    {
    }

    [[nodiscard]] const std::string& raw() const noexcept { return value_; }

    template <typename T>
        requires is_numeric_v<T>
    T as() const
    {
        if (value_.empty())
            throw std::runtime_error{"Number conversion failed"};
        if constexpr (std::is_integral_v<T>)
            return as_integer<T>();
        else
        {
            T result{};
            const auto parsed = std::from_chars(value_.data(), value_.data() + value_.size(), result);
            if (parsed.ec != std::errc{} || parsed.ptr != value_.data() + value_.size() || !std::isfinite(result))
                throw std::runtime_error{"Number conversion failed"};
            return result;
        }
    }

    template <typename T>
        requires is_numeric_v<T>
    operator T() const
    {
        return as<T>();
    }
};
