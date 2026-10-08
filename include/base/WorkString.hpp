#pragma once
#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include "absl/status/status.h"
#include "absl/status/statusor.h"


inline absl::StatusOr<const char*> read_to_value(const std::string& s)
{
    const char* v = s.c_str();
    const auto invalid_utf8 = [](std::size_t pos)
    {
        return absl::InvalidArgumentError("Invalid UTF-8 at byte " + std::to_string(pos));
    };
    std::size_t pos = 0;
    while (pos < s.size())
    {
        const auto first = static_cast<unsigned char>(v[pos]);
        if (first < 0x80)
        {
            ++pos;
            continue;
        }
        const std::size_t length = first >= 0xc2 && first <= 0xdf ? 2 :
            first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
        if (length == 0 || s.size() - pos < length)
            return invalid_utf8(pos);
        for (std::size_t index = 1; index < length; ++index)
        {
            const auto byte = static_cast<unsigned char>(v[pos + index]);
            if ((byte & 0xc0) != 0x80)
                return invalid_utf8(pos);
        }
        const auto second = static_cast<unsigned char>(v[pos + 1]);
        if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
            (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90))
            return invalid_utf8(pos);
        pos += length;
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

