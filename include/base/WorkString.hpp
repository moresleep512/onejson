#pragma once
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <string>
#include "absl/container/inlined_vector.h"
#include "absl/status/statusor.h"


inline std::string next_char(const std::string& s, size_t pos)
{
    uint8_t c = s[pos];
    int len;
    if (c < 0x80)len = 1;
    else if ((c >> 5) == 0b110)len = 2;
    else if ((c >> 4)==0b1110)len = 3;
    else if ((c >> 3)==0b11110)len = 4;
    else
    {
        pos++;
        return "?";
    }
    std::string ch =s.substr(pos, len);
    pos+=len;
    return ch;
}

inline absl::StatusOr<absl::InlinedVector<std::string, 16>> read_to_value(const std::string& s)
{
    absl::InlinedVector<std::string, 16> v;
    size_t pos = 0;
    while (pos < s.size())
    {
        const auto begin = pos;
        const auto first = static_cast<unsigned char>(s[pos]);
        const size_t length = first < 0x80 ? 1 : first >= 0xc2 && first <= 0xdf ? 2 :
            first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
        if (length == 0 || length > s.size() - pos)
            return absl::InvalidArgumentError("Invalid UTF-8 at byte " + std::to_string(begin));
        for (size_t index = 1; index < length; ++index)
        {
            const auto byte = static_cast<unsigned char>(s[pos + index]);
            if (byte < 0x80 || byte > 0xbf)
                return absl::InvalidArgumentError("Invalid UTF-8 at byte " + std::to_string(pos + index));
        }
        if (length >= 3)
        {
            const auto second = static_cast<unsigned char>(s[pos + 1]);
            if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
                (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90))
                return absl::InvalidArgumentError("Invalid UTF-8 at byte " + std::to_string(begin));
        }
        auto ch = next_char(s, pos);
        pos += ch.size();
        v.push_back(std::move(ch));
    }
    return v;
}

inline absl::StatusOr<std::string> path_to_string(const std::string& filepath)
{
    if (filepath.empty() || filepath.find('\0') != std::string::npos)
        return absl::InvalidArgumentError("JSON file path is empty or contains a null byte");
    std::filesystem::path path(filepath);
    std::error_code error;
    const auto file_status = std::filesystem::status(path, error);
    if (error)
        return absl::ErrnoToStatus(error.default_error_condition().value(), "Cannot inspect JSON file: " + filepath);
    if (!std::filesystem::exists(file_status))
        return absl::NotFoundError("JSON file does not exist: " + filepath);
    if (!std::filesystem::is_regular_file(file_status))
        return absl::FailedPreconditionError("JSON path is not a regular file: " + filepath);
    errno = 0;
    std::ifstream fin(path,std::ios::binary);
    if (!fin)
    {
        const int error_number = errno;
        if (error_number != 0)
            return absl::ErrnoToStatus(error_number, "Cannot open JSON file: " + filepath);
        return absl::UnknownError("Cannot open JSON file: " + filepath);
    }
    try
    {
        std::string content(std::istreambuf_iterator<char>(fin), std::istreambuf_iterator<char>{});
        if (fin.bad())
            return absl::DataLossError("Cannot read JSON file: " + filepath);
        return content;
    }
    catch (const std::ios_base::failure&)
    {
        return absl::DataLossError("Cannot read JSON file: " + filepath);
    }
}


