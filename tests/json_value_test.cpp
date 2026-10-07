#include "../include/onejson.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace
{
    TEST(JsonValueTest, StoresNestedArraysAndObjects)
    {
        JsonValue::Object child;
        child.emplace("name", JsonValue{std::string{"onejson"}});
        JsonValue::Array items{JsonValue{Number{42}}, JsonValue{true}, JsonValue{std::move(child)}};
        JsonValue::Object root;
        root.emplace("items", JsonValue{std::move(items)});
        const JsonValue value{std::move(root)};

        ASSERT_EQ(value["items"].get<JsonValue::Array>().size(), 3);
        EXPECT_EQ(value["items"][0].get<Number>().raw(), "42");
        EXPECT_TRUE(value["items"][1].get<bool>());
        EXPECT_EQ(value["items"][2]["name"].get<std::string>(), "onejson");
    }

    TEST(JsonValueTest, CopiesNestedContainersIndependently)
    {
        JsonValue::Object object;
        object.emplace("values", JsonValue{JsonValue::Array{JsonValue{Number{42}}}});
        const JsonValue original{std::move(object)};
        JsonValue copy = original;

        auto& copiedArray = copy["values"].get<JsonValue::Array>();
        copy["values"][0] = 7;
        copiedArray.push_back(JsonValue{false});

        ASSERT_EQ(original["values"].get<JsonValue::Array>().size(), 1);
        EXPECT_EQ(original["values"][0].get<Number>().raw(), "42");
        ASSERT_EQ(copiedArray.size(), 2);
        EXPECT_EQ(copy["values"][0].get<Number>().raw(), "7");
    }

    TEST(JsonValueTest, SupportsChainedObjectAndArrayAssignments)
    {
        Json json;
        json["user"]["addresses"][0]["city"] = "Shanghai";
        json["user"]["age"] = 42;
        json["user"]["enabled"] = true;
        json["user"]["optional"] = nullptr;

        const Json& readOnly = json;
        EXPECT_EQ(readOnly["user"]["addresses"][0]["city"].get<std::string>(), "Shanghai");
        EXPECT_EQ(readOnly["user"]["age"].get<Number>().as<int>(), 42);
        EXPECT_TRUE(readOnly["user"]["enabled"].get<bool>());
        EXPECT_TRUE(readOnly["user"]["optional"].is_null());
    }

    TEST(JsonValueTest, SparseArrayGrowthPreservesExistingValues)
    {
        JsonValue array;
        array[0] = "first";
        array[2] = 7;

        ASSERT_EQ(array.get<JsonValue::Array>().size(), 3);
        EXPECT_EQ(array[0].get<std::string>(), "first");
        EXPECT_TRUE(array[1].is_null());
        EXPECT_EQ(array[2].get<Number>().as<int>(), 7);
    }

    TEST(JsonValueTest, ConstIndexingChecksMissingKeysAndArrayBounds)
    {
        const Json json;
        EXPECT_THROW(static_cast<void>(json["missing"]), std::out_of_range);

        const JsonValue object{JsonValue::Object{}};
        EXPECT_THROW(static_cast<void>(object["missing"]), std::out_of_range);
        EXPECT_TRUE(object.get<JsonValue::Object>().empty());

        const JsonValue array{JsonValue::Array{JsonValue{true}}};
        EXPECT_THROW(static_cast<void>(array[1]), std::out_of_range);
        EXPECT_EQ(array.get<JsonValue::Array>().size(), 1);
    }

    TEST(JsonValueTest, IndexingDoesNotOverwriteScalarValues)
    {
        JsonValue number = 42;
        EXPECT_THROW(static_cast<void>(number["key"]), std::bad_variant_access);
        EXPECT_THROW(static_cast<void>(number[0]), std::bad_variant_access);
        EXPECT_EQ(number.get<Number>().as<int>(), 42);
    }

    TEST(JsonValueTest, RejectsOversizedArrayIndex)
    {
        JsonValue array{JsonValue::Array{}};
        EXPECT_THROW(static_cast<void>(array[std::numeric_limits<std::size_t>::max()]), std::out_of_range);
        EXPECT_TRUE(array.get<JsonValue::Array>().empty());
    }
}
