#pragma once
#include <stack>
#include <string_view>

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
        const auto& content = *content_handle;
        size_t pos = 0;
        const auto invalid = [&](const std::string& message)
        {
            return absl::InvalidArgumentError(message + " at token " + std::to_string(pos));
        };
        const auto skip_space = [&]
        {
            while (pos < content.size() && (content[pos] == " " || content[pos] == "\t" ||
                                           content[pos] == "\r" || content[pos] == "\n"))
                ++pos;
        };
        const auto read_hex = [&]() -> absl::StatusOr<unsigned>
        {
            unsigned code = 0;
            for (int index = 0; index < 4; ++index)
            {
                if (pos == content.size() || content[pos].size() != 1)
                    return invalid("Incomplete Unicode escape");
                const char ch = content[pos++][0];
                const int digit = ch >= '0' && ch <= '9' ? ch - '0' :
                    ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
                if (digit < 0)
                    return invalid("Invalid Unicode escape");
                code = code * 16 + static_cast<unsigned>(digit);
            }
            return code;
        };
        const auto read_string = [&]() -> absl::StatusOr<std::string>
        {
            if (pos == content.size() || content[pos] != "\"")
                return invalid("Expected a quoted string");
            ++pos;
            std::string result;
            while (pos < content.size())
            {
                const auto& ch = content[pos++];
                if (ch == "\"")
                    return result;
                if (ch != "\\")
                {
                    if (static_cast<unsigned char>(ch[0]) < 0x20)
                        return invalid("Unescaped control character in string");
                    result += ch;
                    continue;
                }
                if (pos == content.size())
                    return invalid("Incomplete string escape");
                const auto& escape = content[pos++];
                if (escape == "\"" || escape == "\\" || escape == "/") result += escape;
                else if (escape == "b") result += '\b';
                else if (escape == "f") result += '\f';
                else if (escape == "n") result += '\n';
                else if (escape == "r") result += '\r';
                else if (escape == "t") result += '\t';
                else if (escape == "u")
                {
                    auto code_handle = read_hex();
                    if (!code_handle.ok())
                        return code_handle.status();
                    unsigned code = *code_handle;
                    if (code >= 0xd800 && code <= 0xdbff)
                    {
                        if (content.size() - pos < 2 || content[pos] != "\\" || content[pos + 1] != "u")
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
                        result += static_cast<char>(code);
                    else
                    {
                        if (code < 0x800)
                            result += static_cast<char>(0xc0 | (code >> 6));
                        else
                        {
                            if (code < 0x10000)
                                result += static_cast<char>(0xe0 | (code >> 12));
                            else
                            {
                                result += static_cast<char>(0xf0 | (code >> 18));
                                result += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
                            }
                            result += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
                        }
                        result += static_cast<char>(0x80 | (code & 0x3f));
                    }
                }
                else
                    return invalid("Invalid string escape");
            }
            return invalid("Unterminated string");
        };

        skip_space();
        if (pos == content.size() || content[pos] != "{")
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
            if (pos == content.size())
                return invalid("Incomplete JSON object or array");
            auto& parent = parents.top();
            const bool object = std::holds_alternative<JsonValue::Object>(parent.container->value_);
            const std::string closing = object ? "}" : "]";
            if (parent.state == State::KeyOrEnd || parent.state == State::Key)
            {
                if (parent.state == State::KeyOrEnd && content[pos] == closing)
                {
                    ++pos;
                    parents.pop();
                    continue;
                }
                auto key = read_string();
                if (!key.ok())
                    return key.status();
                auto [it, inserted] = parent.container->get<JsonValue::Object>().try_emplace(*key);
                if (!inserted)
                    return absl::AlreadyExistsError("Duplicate JSON object key: " + *key);
                parent.member = &it->second;
                parent.state = State::Colon;
                continue;
            }
            if (parent.state == State::Colon)
            {
                if (content[pos++] != ":")
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
                if (content[pos++] != ",")
                    return invalid("Expected ',' or '" + closing + "'");
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
            if (content[pos] == "{" || content[pos] == "[")
            {
                const bool child_object = content[pos++] == "{";
                if (child_object) value->value_.emplace<JsonValue::Object>();
                else value->value_.emplace<JsonValue::Array>();
                parents.push({value, child_object ? State::KeyOrEnd : State::ValueOrEnd});
            }
            else if (content[pos] == "\"")
            {
                auto text = read_string();
                if (!text.ok())
                    return text.status();
                *value = std::move(*text);
            }
            else
            {
                std::string literal;
                while (pos < content.size() && content[pos].find_first_of(" \t\r\n{}[],:\"") == std::string::npos)
                    literal += content[pos++];
                if (literal == "true") *value = true;
                else if (literal == "false") *value = false;
                else if (literal == "null") *value = nullptr;
                else
                {
                    try
                    {
                        *value = Number{literal};
                    }
                    catch (const std::invalid_argument&)
                    {
                        return invalid("Invalid JSON value: " + literal);
                    }
                }
            }
        }
        skip_space();
        if (pos != content.size())
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
