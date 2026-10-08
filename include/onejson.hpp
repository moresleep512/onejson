#pragma once
#include <cstddef>
#include <stack>
#include <string>
#include <string_view>
#include <utility>

#include "base/JsonValue.hpp"
#include "base/WorkString.hpp"
class Json
{
    JsonValue::Object data_;

    absl::Status init(const std::string& s)
    {
        auto file = path_to_string(s);
        if (!file.ok())
            return file.status();
        auto content_handle = read_to_value(*file);
        if (!content_handle.ok())
            return content_handle.status();
        const char* content = *content_handle;
        const auto size = file->size();
        std::size_t pos = 0;
        std::string buffer;
        const auto invalid = [&](const std::string& message)
        {
            return absl::InvalidArgumentError(message + " at byte " + std::to_string(pos));
        };
        const auto invalid_utf8 = [](std::size_t begin)
        {
            return absl::InvalidArgumentError("Invalid UTF-8 at byte " + std::to_string(begin));
        };
        const auto skip_space = [&]
        {
            while (pos < size && (content[pos] == ' ' || content[pos] == '\t' ||
                                 content[pos] == '\r' || content[pos] == '\n'))
                ++pos;
        };
        const auto read_hex = [&]() -> absl::StatusOr<unsigned>
        {
            unsigned code = 0;
            for (int index = 0; index < 4; ++index)
            {
                if (pos == size)
                    return invalid("Incomplete Unicode escape");
                const char ch = content[pos++];
                const int digit = ch >= '0' && ch <= '9' ? ch - '0' :
                    ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
                if (digit < 0)
                    return invalid("Invalid Unicode escape");
                code = code * 16 + static_cast<unsigned>(digit);
            }
            return code;
        };
        const auto read_string = [&]() -> absl::Status
        {
            buffer.clear();
            if (pos == size || content[pos] != '"')
                return invalid("Expected a quoted string");
            ++pos;
            while (pos < size)
            {
                const char ch = content[pos++];
                if (ch == '"')
                    return absl::OkStatus();
                if (ch != '\\')
                {
                    const auto byte = static_cast<unsigned char>(ch);
                    if (byte < 0x20)
                        return invalid("Unescaped control character in string");
                    if (byte < 0x80)
                    {
                        buffer += ch;
                        continue;
                    }
                    const auto begin = pos - 1;
                    const unsigned remaining = byte >= 0xc2 && byte <= 0xdf ? 1 :
                        byte >= 0xe0 && byte <= 0xef ? 2 : byte >= 0xf0 && byte <= 0xf4 ? 3 : 0;
                    if (remaining == 0 || size - pos < remaining)
                        return invalid_utf8(begin);
                    unsigned code = byte & ((1u << (6 - remaining)) - 1);
                    for (unsigned index = 0; index < remaining; ++index)
                    {
                        const auto next = static_cast<unsigned char>(content[pos + index]);
                        if ((next & 0xc0) != 0x80)
                            return invalid_utf8(begin);
                        code = (code << 6) | (next & 0x3f);
                    }
                    const unsigned minimum = remaining == 1 ? 0x80 : remaining == 2 ? 0x800 : 0x10000;
                    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
                        return invalid_utf8(begin);
                    buffer += ch;
                    for (unsigned index = 0; index < remaining; ++index)
                        buffer += content[pos++];
                    continue;
                }
                if (pos == size)
                    return invalid("Incomplete string escape");
                const char escape = content[pos++];
                if (escape == '"' || escape == '\\' || escape == '/') buffer += escape;
                else if (escape == 'b') buffer += '\b';
                else if (escape == 'f') buffer += '\f';
                else if (escape == 'n') buffer += '\n';
                else if (escape == 'r') buffer += '\r';
                else if (escape == 't') buffer += '\t';
                else if (escape == 'u')
                {
                    auto code_handle = read_hex();
                    if (!code_handle.ok())
                        return code_handle.status();
                    unsigned code = *code_handle;
                    if (code >= 0xd800 && code <= 0xdbff)
                    {
                        if (size - pos < 2 || content[pos] != '\\' || content[pos + 1] != 'u')
                            return invalid("Missing low Unicode surrogate");
                        pos += 2;
                        auto low = read_hex();
                        if (!low.ok())
                            return low.status();
                        if (*low < 0xdc00 || *low > 0xdfff)
                            return invalid("Invalid low Unicode surrogate");
                        code = 0x10000 + (code - 0xd800) * 0x400 + (*low - 0xdc00);
                    }
                    else if (code >= 0xdc00 && code <= 0xdfff)
                        return invalid("Unexpected low Unicode surrogate");
                    if (code < 0x80)
                        buffer += static_cast<char>(code);
                    else
                    {
                        if (code < 0x800)
                            buffer += static_cast<char>(0xc0 | (code >> 6));
                        else
                        {
                            if (code < 0x10000)
                                buffer += static_cast<char>(0xe0 | (code >> 12));
                            else
                            {
                                buffer += static_cast<char>(0xf0 | (code >> 18));
                                buffer += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
                            }
                            buffer += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
                        }
                        buffer += static_cast<char>(0x80 | (code & 0x3f));
                    }
                }
                else
                    return invalid("Invalid string escape");
            }
            return invalid("Unterminated string");
        };

        skip_space();
        if (pos == size || content[pos] != '{')
            return invalid("JSON root must be an object");
        ++pos;
        enum class State { KeyOrEnd, Key, Colon, ValueOrEnd, Value, CommaOrEnd };
        struct Parent
        {
            JsonValue* container;
            State state;
            JsonValue* member = nullptr;
        };
        JsonValue root{JsonValue::Object{}};
        std::stack<Parent> parents;
        parents.push({&root, State::KeyOrEnd});
        while (!parents.empty())
        {
            skip_space();
            if (pos == size)
                return invalid("Incomplete JSON object or array");
            auto& parent = parents.top();
            const bool object = std::holds_alternative<JsonValue::Object>(parent.container->value_);
            const char closing = object ? '}' : ']';
            if (parent.state == State::KeyOrEnd || parent.state == State::Key)
            {
                if (parent.state == State::KeyOrEnd && content[pos] == closing)
                {
                    ++pos;
                    parents.pop();
                    continue;
                }
                auto status = read_string();
                if (!status.ok())
                    return status;
                auto [it, inserted] = parent.container->get<JsonValue::Object>().try_emplace(std::move(buffer));
                if (!inserted)
                    return absl::AlreadyExistsError("Duplicate JSON object key: " + it->first);
                parent.member = &it->second;
                parent.state = State::Colon;
                continue;
            }
            if (parent.state == State::Colon)
            {
                if (content[pos++] != ':')
                    return invalid("Expected ':' after object key");
                parent.state = State::Value;
                continue;
            }
            if (parent.state == State::CommaOrEnd)
            {
                if (content[pos] == closing)
                {
                    ++pos;
                    parents.pop();
                    continue;
                }
                if (content[pos++] != ',')
                    return invalid(std::string("Expected ',' or '") + closing + "'");
                parent.state = object ? State::Key : State::Value;
                continue;
            }
            if (parent.state == State::ValueOrEnd && content[pos] == closing)
            {
                ++pos;
                parents.pop();
                continue;
            }

            JsonValue* value = object ? parent.member : &parent.container->get<JsonValue::Array>().emplace_back();
            parent.state = State::CommaOrEnd;
            if (content[pos] == '{' || content[pos] == '[')
            {
                const bool child_object = content[pos++] == '{';
                if (child_object) value->value_.emplace<JsonValue::Object>();
                else value->value_.emplace<JsonValue::Array>();
                parents.push({value, child_object ? State::KeyOrEnd : State::ValueOrEnd});
            }
            else if (content[pos] == '"')
            {
                auto status = read_string();
                if (!status.ok())
                    return status;
                *value = std::move(buffer);
            }
            else
            {
                buffer.clear();
                const auto begin = pos;
                constexpr std::string_view delimiters = " \t\r\n{}[],:\"";
                while (pos < size && delimiters.find(content[pos]) == std::string_view::npos)
                    buffer += content[pos++];
                if (buffer == "true") *value = true;
                else if (buffer == "false") *value = false;
                else if (buffer == "null") *value = nullptr;
                else
                {
                    try
                    {
                        *value = Number{std::move(buffer)};
                    }
                    catch (const std::invalid_argument&)
                    {
                        return invalid("Invalid JSON value: " + std::string(content + begin, pos - begin));
                    }
                }
            }
        }
        skip_space();
        if (pos != size)
            return invalid("Unexpected content after JSON root");
        data_ = std::move(root.get<JsonValue::Object>());
        return absl::OkStatus();
    };


public:
    Json() = default;

    explicit Json(const std::string& s)
    {
        auto status = init(s);
        if (!status.ok())
        {
            throw std::runtime_error(status.ToString());
        }
    }

    JsonValue& operator[](const std::string_view key) { return data_[key]; }

    const JsonValue& operator[](const std::string_view key) const { return data_.at(key); }
};
