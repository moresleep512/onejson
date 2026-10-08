#include "../include/onejson.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
    class JsonInitTest : public testing::Test
    {
    protected:
        std::filesystem::path directory;
        std::filesystem::path file;

        void SetUp() override
        {
            static std::atomic<unsigned> sequence{0};
            const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
            directory = std::filesystem::path(testing::TempDir()) /
                ("onejson-test-" + std::to_string(timestamp) + "-" + std::to_string(sequence++));
            ASSERT_TRUE(std::filesystem::create_directory(directory));
            file = directory / "input.json";
        }

        void TearDown() override
        {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
            EXPECT_FALSE(error) << error.message();
        }

        void Write(const std::string& content)
        {
            std::ofstream stream(file, std::ios::binary);
            ASSERT_TRUE(stream.is_open());
            stream.write(content.data(), static_cast<std::streamsize>(content.size()));
            stream.close();
            ASSERT_TRUE(stream);
        }

        static void ExpectError(const std::string& path, absl::StatusCode code, const std::string& message)
        {
            try
            {
                const Json json(path);
                FAIL() << "Expected initialization to fail";
            }
            catch (const std::runtime_error& error)
            {
                const std::string text = error.what();
                EXPECT_TRUE(text.starts_with(std::string(absl::StatusCodeToString(code)) + ":")) << text;
                EXPECT_NE(text.find(message), std::string::npos) << text;
            }
        }
    };

    TEST_F(JsonInitTest, ReportsMissingFile)
    {
        const auto result = path_to_string(file.string());
        ASSERT_FALSE(result.ok());
        EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
        ExpectError(file.string(), absl::StatusCode::kNotFound, file.string());
    }

    TEST_F(JsonInitTest, RejectsInvalidPaths)
    {
        for (const auto& path : {std::string{}, file.string() + std::string("\0ignored", 8)})
        {
            const auto result = path_to_string(path);
            ASSERT_FALSE(result.ok());
            EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
            ExpectError(path, absl::StatusCode::kInvalidArgument, "path");
        }
    }

    TEST_F(JsonInitTest, RejectsDirectory)
    {
        const auto result = path_to_string(directory.string());
        ASSERT_FALSE(result.ok());
        EXPECT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);
        ExpectError(directory.string(), absl::StatusCode::kFailedPrecondition, "regular file");
    }

#ifdef _WIN32
    TEST_F(JsonInitTest, ReportsPermissionDeniedWhenFileIsLocked)
    {
        Write("{}");
        const HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL, nullptr);
        ASSERT_NE(handle, INVALID_HANDLE_VALUE);
        struct FileLock
        {
            HANDLE handle;
            ~FileLock() { CloseHandle(handle); }
        } lock{handle};

        const auto result = path_to_string(file.string());
        ASSERT_FALSE(result.ok());
        EXPECT_EQ(result.status().code(), absl::StatusCode::kPermissionDenied);
        ExpectError(file.string(), absl::StatusCode::kPermissionDenied, file.string());
    }
#endif

    TEST_F(JsonInitTest, ReadsFileWithoutChangingContent)
    {
        const std::string content = "{\r\n\t\"name\": \"onejson\"\r\n}\n";
        Write(content);
        const auto result = path_to_string(file.string());
        ASSERT_TRUE(result.ok()) << result.status();
        EXPECT_EQ(*result, content);
    }

    TEST_F(JsonInitTest, InitializesEmptyObject)
    {
        Write(" \r\n\t{ }\n");
        const Json json(file.string());
        EXPECT_THROW(static_cast<void>(json["missing"]), std::out_of_range);
    }

    TEST_F(JsonInitTest, InitializesScalarValues)
    {
        Write(R"({"text":"onejson","empty":"","number":-1.2500e+03,"enabled":true,"disabled":false,"nothing":null})");
        const Json json(file.string());
        EXPECT_EQ(json["text"].get<std::string>(), "onejson");
        EXPECT_EQ(json["empty"].get<std::string>(), "");
        EXPECT_EQ(json["number"].get<Number>().raw(), "-1.2500e+03");
        EXPECT_TRUE(json["enabled"].get<bool>());
        EXPECT_FALSE(json["disabled"].get<bool>());
        EXPECT_TRUE(json["nothing"].is_null());
    }

    TEST_F(JsonInitTest, InitializesNestedObjectsAndArrays)
    {
        Write(R"({"items":[42,true,null,"text",{"name":"onejson"},[false,{}]],"emptyArray":[],"emptyObject":{},"after":7})");
        const Json json(file.string());
        ASSERT_EQ(json["items"].get<JsonValue::Array>().size(), 6);
        EXPECT_EQ(json["items"][0].get<Number>().as<int>(), 42);
        EXPECT_TRUE(json["items"][1].get<bool>());
        EXPECT_TRUE(json["items"][2].is_null());
        EXPECT_EQ(json["items"][3].get<std::string>(), "text");
        EXPECT_EQ(json["items"][4]["name"].get<std::string>(), "onejson");
        EXPECT_FALSE(json["items"][5][0].get<bool>());
        EXPECT_TRUE(json["items"][5][1].get<JsonValue::Object>().empty());
        EXPECT_TRUE(json["emptyArray"].get<JsonValue::Array>().empty());
        EXPECT_TRUE(json["emptyObject"].get<JsonValue::Object>().empty());
        EXPECT_EQ(json["after"].get<Number>().as<int>(), 7);
    }

    TEST_F(JsonInitTest, HandlesParentRestorationAndContainerGrowth)
    {
        std::string content = "{\"items\":[";
        for (int index = 0; index < 100; ++index)
        {
            if (index != 0) content += ',';
            content += "{\"value\":" + std::to_string(index) + "}";
        }
        content += "]";
        for (int index = 0; index < 100; ++index)
            content += ",\"key" + std::to_string(index) + "\":{\"value\":" + std::to_string(index) + "}";
        content += "}";
        Write(content);
        const Json json(file.string());
        ASSERT_EQ(json["items"].get<JsonValue::Array>().size(), 100);
        for (int index = 0; index < 100; ++index)
        {
            EXPECT_EQ(json["items"][index]["value"].get<Number>().as<int>(), index);
            EXPECT_EQ(json["key" + std::to_string(index)]["value"].get<Number>().as<int>(), index);
        }
    }

    TEST_F(JsonInitTest, DecodesStringsAndUnicodeEscapes)
    {
        Write(R"({"text":"\"\\\/\b\f\n\r\t","unicode":"\u0041\u00e9\u4e2d\ud83d\ude00","\u006bey":"\u0000","punctuation":"{}[]:,"})");
        const Json json(file.string());
        EXPECT_EQ(json["text"].get<std::string>(), "\"\\/\b\f\n\r\t");
        EXPECT_EQ(json["unicode"].get<std::string>(), "A\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80");
        EXPECT_EQ(json["key"].get<std::string>(), std::string(1, '\0'));
        EXPECT_EQ(json["punctuation"].get<std::string>(), "{}[]:,");
    }

    TEST_F(JsonInitTest, PreservesUtf8Strings)
    {
        const std::string text = "\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80";
        Write("{\"" + text + "\":\"" + text + "\"}");
        const Json json(file.string());
        EXPECT_EQ(json[text].get<std::string>(), text);
    }

    TEST_F(JsonInitTest, PreservesNumbersBeyondNativeRange)
    {
        const std::string number(1000, '9');
        Write("{\"large\":" + number + ",\"exponent\":1e100000}");
        const Json json(file.string());
        EXPECT_EQ(json["large"].get<Number>().raw(), number);
        EXPECT_EQ(json["exponent"].get<Number>().raw(), "1e100000");
    }

    TEST_F(JsonInitTest, ReusesBufferAcrossLongKeysAndMixedValues)
    {
        const std::string key(96, 'k');
        const std::string text(256, 's');
        const std::string number(128, '9');
        Write("{\"" + key + "\":[\"" + text + "\"," + number +
              ",true,false,null,\"\",0,{\"\":\"tail\"}],\"after\":\"done\"}");
        const Json json(file.string());
        const auto& items = json[key].get<JsonValue::Array>();
        ASSERT_EQ(items.size(), 8);
        EXPECT_EQ(items[0].get<std::string>(), text);
        EXPECT_EQ(items[1].get<Number>().raw(), number);
        EXPECT_TRUE(items[2].get<bool>());
        EXPECT_FALSE(items[3].get<bool>());
        EXPECT_TRUE(items[4].is_null());
        EXPECT_TRUE(items[5].get<std::string>().empty());
        EXPECT_EQ(items[6].get<Number>().raw(), "0");
        EXPECT_EQ(items[7][""].get<std::string>(), "tail");
        EXPECT_EQ(json["after"].get<std::string>(), "done");
    }

    TEST_F(JsonInitTest, ReportsInvalidLiteralAfterMovingBuffer)
    {
        const std::string literal = std::string(128, '9') + 'x';
        Write("{\"key\":" + literal + "}");
        ExpectError(file.string(), absl::StatusCode::kInvalidArgument, "Invalid JSON value: " + literal);
    }

    TEST_F(JsonInitTest, RejectsDuplicateKeysInTheSameObject)
    {
        for (const char* content : {R"({"key":1,"key":2})", R"({"key":1,"\u006bey":2})",
                                    R"({"child":{"key":1,"key":2}})"})
        {
            Write(content);
            ExpectError(file.string(), absl::StatusCode::kAlreadyExists, "Duplicate JSON object key: key");
        }
    }

    TEST_F(JsonInitTest, AllowsSameKeysInDifferentObjects)
    {
        Write(R"({"left":{"key":1},"right":{"key":2}})");
        const Json json(file.string());
        EXPECT_EQ(json["left"]["key"].get<Number>().as<int>(), 1);
        EXPECT_EQ(json["right"]["key"].get<Number>().as<int>(), 2);
    }

    class InvalidJsonTest : public JsonInitTest, public testing::WithParamInterface<std::string>
    {
    };

    TEST_P(InvalidJsonTest, ReturnsInvalidArgument)
    {
        SCOPED_TRACE(GetParam());
        Write(GetParam());
        ExpectError(file.string(), absl::StatusCode::kInvalidArgument, "at byte");
    }

    INSTANTIATE_TEST_SUITE_P(
        MalformedInput, InvalidJsonTest,
        testing::Values("", " \t\r\n", "[]", "42", "true", "null", "\"text\"", "}", "{", "{]", "{}{}",
                        "{} trailing", "{,}", "{:1}", "{key:1}", "{\"key\"}", "{\"key\",1}", "{\"key\":}",
                        "{\"key\":1", "{\"key\":1,}", "{\"key\":1 \"other\":2}", "{\"key\"::1}",
                        "{\"key\":[]]}", "{\"key\":[}", "{\"key\":[,]}", "{\"key\":[1,]}", "{\"key\":[1 2]}",
                        "{\"key\":[1,,2]}", "{\"key\":{]}", "{\"key\":True}", "{\"key\":undefined}",
                        "{\"key\":01}", "{\"key\":+1}", "{\"key\":.5}", "{\"key\":1.}", "{\"key\":1e}",
                        "{\"key\":NaN}", "{\"key\":Infinity}", "{\"key\":\"unterminated}", "{\"key\":\"escape\\",
                        "{\"key\":\"\\x\"}", "{\"key\":\"\\u12\"}", "{\"key\":\"\\uZZZZ\"}",
                        "{\"key\":\"\\ud800\"}", "{\"key\":\"\\udc00\"}", "{\"key\":\"\\ud800\\u0041\"}",
                        "{\"key\":\"raw\nnewline\"}", "{\"key\":\"raw\ttab\"}", "{\"key\":1}\v"));

    TEST_F(JsonInitTest, RejectsInvalidUtf8)
    {
        for (const auto& bytes : {std::string("\x80"), std::string("\xc0\xaf"), std::string("\xc2"),
                                  std::string("\xe2\x28\xa1"), std::string("\xe0\x80\x80"),
                                  std::string("\xed\xa0\x80"), std::string("\xf0\x80\x80\x80"),
                                  std::string("\xf4\x90\x80\x80"), std::string("\xf5\x80\x80\x80"),
                                  std::string("\xe2"), std::string("\xe2\x82"), std::string("\xf0"),
                                  std::string("\xf0\x90"), std::string("\xf0\x90\x80"), std::string("\xff")})
        {
            Write("{\"key\":\"" + bytes + "\"}");
            ExpectError(file.string(), absl::StatusCode::kInvalidArgument, "UTF-8 at byte 8");
            Write("{\"" + bytes + "\":1}");
            ExpectError(file.string(), absl::StatusCode::kInvalidArgument, "UTF-8 at byte 2");
            Write("{\"key\":\"" + bytes);
            ExpectError(file.string(), absl::StatusCode::kInvalidArgument, "UTF-8 at byte 8");
        }
    }

    TEST_F(JsonInitTest, PreservesUtf8BoundaryCodePoints)
    {
        const std::string text = "\xc2\x80\xdf\xbf\xe0\xa0\x80\xed\x9f\xbf\xee\x80\x80\xef\xbf\xbf"
                                 "\xf0\x90\x80\x80\xf4\x8f\xbf\xbf";
        Write("{\"" + text + "\":\"" + text + "\"}");
        const Json json(file.string());
        EXPECT_EQ(json[text].get<std::string>(), text);
    }

    TEST_F(JsonInitTest, ReportsByteOffsetAfterUtf8)
    {
        const std::string content = "{\"\xe4\xb8\xad\":1,}";
        Write(content);
        ExpectError(file.string(), absl::StatusCode::kInvalidArgument,
                    "Expected a quoted string at byte " + std::to_string(content.size() - 1));
    }

    TEST_F(JsonInitTest, RejectsRawNullBytesWithoutTruncatingInput)
    {
        Write(std::string("{\"key\":\"raw") + '\0' + "byte\"}");
        ExpectError(file.string(), absl::StatusCode::kInvalidArgument, "Unescaped control character");
        Write(std::string("{}") + '\0' + "trailing");
        ExpectError(file.string(), absl::StatusCode::kInvalidArgument,
                    "Unexpected content after JSON root at byte 2");
        Write(std::string("{\"key\":1") + '\0' + "2}");
        ExpectError(file.string(), absl::StatusCode::kInvalidArgument, "Invalid JSON value");
    }

    TEST(WorkStringTest, ReportsInvalidUtf8AtSequenceStart)
    {
        for (const auto& prefix : {std::string{}, std::string("A\xe4\xb8\xad") + '\0'})
        {
            for (const auto& bytes : {std::string("\x80"), std::string("\xbf"), std::string("\xc0\xaf"),
                                      std::string("\xc1\xbf"), std::string("\xc2"), std::string("\xc2\x7f"),
                                      std::string("\xe2"), std::string("\xe2\x82"), std::string("\xe2\x28\xa1"),
                                      std::string("\xe2\x82\x7f"), std::string("\xe0\x80\x80"),
                                      std::string("\xed\xa0\x80"), std::string("\xf0"), std::string("\xf0\x90"),
                                      std::string("\xf0\x90\x80"), std::string("\xf0\x90\x80\x7f"),
                                      std::string("\xf0\x80\x80\x80"), std::string("\xf4\x90\x80\x80"),
                                      std::string("\xf5\x80\x80\x80"), std::string("\xff")})
            {
                const std::string content = prefix + bytes;
                SCOPED_TRACE(testing::PrintToString(content));
                const auto result = read_to_value(content);
                ASSERT_FALSE(result.ok());
                EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
                EXPECT_EQ(result.status().message(), "Invalid UTF-8 at byte " + std::to_string(prefix.size()));
            }
        }
    }

    TEST(WorkStringTest, BorrowsUtf8BoundaryCodePoints)
    {
        const std::string content = "\x7f\xc2\x80\xdf\xbf\xe0\xa0\x80\xed\x9f\xbf\xee\x80\x80\xef\xbf\xbf"
                                    "\xf0\x90\x80\x80\xf4\x8f\xbf\xbf";
        const auto result = read_to_value(content);
        ASSERT_TRUE(result.ok()) << result.status();
        EXPECT_EQ(*result, content.c_str());
        EXPECT_EQ(std::string_view(*result, content.size()), std::string_view(content));
    }

    TEST_F(JsonInitTest, BorrowsOriginalBytesWithoutSplittingCharacters)
    {
        const std::string content = std::string("A\xe4\xb8\xad\xf0\x9f\x98\x80") + '\0' + "tail";
        const auto result = read_to_value(content);
        ASSERT_TRUE(result.ok()) << result.status();
        EXPECT_EQ(*result, content.c_str());
        EXPECT_EQ(std::string_view(*result, content.size()), std::string_view(content));
    }

    TEST_F(JsonInitTest, BorrowsEmptyInput)
    {
        const std::string content;
        const auto result = read_to_value(content);
        ASSERT_TRUE(result.ok()) << result.status();
        EXPECT_EQ(*result, content.c_str());
        EXPECT_EQ((*result)[0], '\0');
    }
} // namespace
