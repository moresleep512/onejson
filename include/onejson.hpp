#pragma once

#include <string_view>

#include "base/JsonValue.hpp"

class Json
{
    JsonValue::Object data_;

public:
    JsonValue& operator[](std::string_view key) { return data_[key]; }

    const JsonValue& operator[](std::string_view key) const { return data_.at(key); }
};


