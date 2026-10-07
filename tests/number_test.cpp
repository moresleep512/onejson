#include "../include/base/Number.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace
{
    TEST(NumberConstructionTest, PreservesStringRepresentation)
    {
        const std::string text = "1.2500e+03";
        const Number number(text);

        EXPECT_EQ(number.raw(), text);
    }

    TEST(NumberConstructionTest, CopiesCString)
    {
        char text[] = "123.4500";
        const Number number(text);
        text[0] = '9';

        EXPECT_EQ(number.raw(), "123.4500");
    }

    TEST(NumberConstructionTest, AcceptsJsonNumberGrammar)
    {
        for (const char* text : {"0", "-0", "42", "-42", "0.01", "-0.01", "1.2300", "1e3", "1E+03", "1e-3", "-1.23E+4",
                                 "0e999999999999999999999999", "1e100000"})
        {
            SCOPED_TRACE(text);
            EXPECT_EQ(Number(text).raw(), text);
        }
    }

    TEST(NumberConstructionTest, RejectsInvalidJsonNumberGrammar)
    {
        for (const char* text :
             {"",    " ",   "+",     "-",    "abc", "12abc", "+42",  "0042", "-01",      ".5",        "1.",  "1e",
              "1e+", "1e-", "1.2.3", "0x2a", "1 2", " 42",   "42\n", "NaN",  "Infinity", "-Infinity", "--1", "1e+-2"})
        {
            SCOPED_TRACE(text);
            EXPECT_THROW(static_cast<void>(Number(text)), std::invalid_argument);
        }
        EXPECT_THROW(static_cast<void>(Number(std::string("1\0x", 3))), std::invalid_argument);
        EXPECT_THROW(static_cast<void>(Number(static_cast<const char*>(nullptr))), std::invalid_argument);
    }

    TEST(NumberConstructionTest, PreservesNumbersBeyondNativeRange)
    {
        const std::string text(1000, '9');
        const Number number(text);

        EXPECT_EQ(number.raw(), text);
        EXPECT_EQ(number.as<std::uint64_t>(), std::numeric_limits<std::uint64_t>::max());
        EXPECT_THROW(number.as<double>(), std::runtime_error);
    }

    TEST(NumberConstructionTest, RawReturnsStableConstReference)
    {
        const Number number("42");
        const std::string& raw = number.raw();

        static_assert(std::is_same_v<decltype(number.raw()), const std::string&>);
        static_assert(noexcept(number.raw()));
        EXPECT_EQ(&raw, &number.raw());
        EXPECT_EQ(number.as<int>(), 42);
        EXPECT_EQ(raw, "42");
    }

    template <typename T>
    class NumberIntegerTest : public testing::Test
    {
    };

    using IntegerTypes = testing::Types<std::int8_t, std::uint8_t, std::int16_t, std::uint16_t, std::int32_t,
                                        std::uint32_t, std::int64_t, std::uint64_t>;
    TYPED_TEST_SUITE(NumberIntegerTest, IntegerTypes);

    TYPED_TEST(NumberIntegerTest, ConstructsDecimalAndRoundTrips)
    {
        const Number zero(TypeParam{0});
        const Number positive(TypeParam{42});

        EXPECT_EQ(zero.raw(), "0");
        EXPECT_EQ(zero.template as<TypeParam>(), TypeParam{0});
        EXPECT_EQ(positive.raw(), "42");
        EXPECT_EQ(positive.template as<TypeParam>(), TypeParam{42});

        if constexpr (std::is_signed_v<TypeParam>)
        {
            const Number negative(TypeParam{-42});
            EXPECT_EQ(negative.raw(), "-42");
            EXPECT_EQ(negative.template as<TypeParam>(), TypeParam{-42});
        }
    }

    TYPED_TEST(NumberIntegerTest, RoundTripsLimits)
    {
        const auto minimum = std::numeric_limits<TypeParam>::min();
        const auto maximum = std::numeric_limits<TypeParam>::max();
        const Number low(minimum);
        const Number high(maximum);

        EXPECT_EQ(low.raw(), std::to_string(minimum));
        EXPECT_EQ(high.raw(), std::to_string(maximum));
        EXPECT_EQ(low.template as<TypeParam>(), minimum);
        EXPECT_EQ(high.template as<TypeParam>(), maximum);
    }

    TYPED_TEST(NumberIntegerTest, PreservesNegativeZero)
    {
        const Number number("-0");

        EXPECT_EQ(number.template as<TypeParam>(), TypeParam{0});
        EXPECT_EQ(number.raw(), "-0");
    }

    TYPED_TEST(NumberIntegerTest, SupportsImplicitConversion)
    {
        const Number number("42");
        const TypeParam value = number;

        EXPECT_EQ(value, TypeParam{42});
    }

    TYPED_TEST(NumberIntegerTest, ClampsValuesOutsideTypeRange)
    {
        const auto maximum = std::numeric_limits<TypeParam>::max();
        const std::string overflow = sizeof(TypeParam) < sizeof(std::uint64_t)
            ? std::to_string(static_cast<std::uint64_t>(maximum) + 1)
            : (std::is_signed_v<TypeParam> ? "9223372036854775808" : "18446744073709551616");
        const Number tooHigh(overflow);
        EXPECT_EQ(tooHigh.template as<TypeParam>(), maximum);
        EXPECT_EQ(static_cast<TypeParam>(tooHigh), maximum);

        if constexpr (std::is_signed_v<TypeParam>)
        {
            const std::string underflow = sizeof(TypeParam) < sizeof(std::int64_t)
                ? std::to_string(static_cast<std::int64_t>(std::numeric_limits<TypeParam>::min()) - 1)
                : "-9223372036854775809";
            const Number tooLow(underflow);
            EXPECT_EQ(tooLow.template as<TypeParam>(), std::numeric_limits<TypeParam>::min());
            EXPECT_EQ(static_cast<TypeParam>(tooLow), std::numeric_limits<TypeParam>::min());
        }
        else
        {
            const Number negative("-1");
            EXPECT_EQ(negative.template as<TypeParam>(), TypeParam{0});
        }
    }

    TYPED_TEST(NumberIntegerTest, EvaluatesDecimalAndScientificNotation)
    {
        EXPECT_EQ(Number("1.9").template as<TypeParam>(), TypeParam{1});
        EXPECT_EQ(Number("4.29e1").template as<TypeParam>(), TypeParam{42});
        EXPECT_EQ(Number("1234e-2").template as<TypeParam>(), TypeParam{12});
        EXPECT_EQ(Number("0.00429E+4").template as<TypeParam>(), TypeParam{42});
        EXPECT_EQ(Number("0.99").template as<TypeParam>(), TypeParam{0});
        EXPECT_EQ(Number("1e-3").template as<TypeParam>(), TypeParam{0});

        if constexpr (std::is_signed_v<TypeParam>)
        {
            EXPECT_EQ(Number("-1.9").template as<TypeParam>(), TypeParam{-1});
            EXPECT_EQ(Number("-4.29e1").template as<TypeParam>(), TypeParam{-42});
            EXPECT_EQ(Number("-0.99").template as<TypeParam>(), TypeParam{0});
        }
        else
        {
            EXPECT_EQ(Number("-1.9").template as<TypeParam>(), TypeParam{0});
            EXPECT_EQ(Number("-4.29e1").template as<TypeParam>(), TypeParam{0});
        }
    }

    TYPED_TEST(NumberIntegerTest, HandlesHugeExponentsAndZero)
    {
        EXPECT_EQ(Number("1e999999999999999999999999999999").template as<TypeParam>(),
                  std::numeric_limits<TypeParam>::max());
        EXPECT_EQ(Number("-1e999999999999999999999999999999").template as<TypeParam>(),
                  std::numeric_limits<TypeParam>::min());
        EXPECT_EQ(Number("1e-999999999999999999999999999999").template as<TypeParam>(), TypeParam{0});
        EXPECT_EQ(Number("0e999999999999999999999999999999").template as<TypeParam>(), TypeParam{0});
        EXPECT_EQ(Number("-0.000e999999999999999999999999999999").template as<TypeParam>(), TypeParam{0});
    }

    TYPED_TEST(NumberIntegerTest, TruncatesAtExactIntegerBoundaries)
    {
        const auto maximum = std::numeric_limits<TypeParam>::max();
        const auto minimum = std::numeric_limits<TypeParam>::min();
        const Number high(std::to_string(maximum) + ".999999999999999999");
        const Number low(std::to_string(minimum) + ".999999999999999999");

        EXPECT_EQ(high.template as<TypeParam>(), maximum);
        EXPECT_EQ(low.template as<TypeParam>(), minimum);
    }

    TEST(NumberConversionTest, EvaluatesScientificNotationBeforeIntegerConversion)
    {
        const Number number("1e3");

        EXPECT_EQ(number.as<int>(), 1000);
        EXPECT_EQ(number.raw(), "1e3");
        EXPECT_EQ(Number("9007199254740993.999").as<std::int64_t>(), INT64_C(9007199254740993));
        EXPECT_EQ(Number("9.223372036854775807e18").as<std::int64_t>(), std::numeric_limits<std::int64_t>::max());
        EXPECT_EQ(Number("-9.223372036854775808e18").as<std::int64_t>(), std::numeric_limits<std::int64_t>::min());
        EXPECT_EQ(Number("1.8446744073709551615e19").as<std::uint64_t>(), std::numeric_limits<std::uint64_t>::max());
        EXPECT_EQ(Number("1.8446744073709551616e19").as<std::uint64_t>(), std::numeric_limits<std::uint64_t>::max());
    }

    TEST(NumberConversionTest, HandlesLargeMantissasWithCompensatingExponents)
    {
        const Number large("42" + std::string(1000, '0') + "e-1000");
        const Number small("0." + std::string(1000, '0') + "42e1002");

        EXPECT_EQ(large.as<int>(), 42);
        EXPECT_EQ(small.as<int>(), 42);
    }

    template <typename T>
    class NumberFloatingPointTest : public testing::Test
    {
    };

    using FloatingPointTypes = testing::Types<float, double, long double>;
    TYPED_TEST_SUITE(NumberFloatingPointTest, FloatingPointTypes);

    TYPED_TEST(NumberFloatingPointTest, ConstructsDecimalRepresentation)
    {
        EXPECT_EQ(Number(TypeParam{0}).raw(), "0");
        EXPECT_EQ(Number(TypeParam{1.25}).raw(), "1.25");
        EXPECT_EQ(Number(TypeParam{-2.5}).raw(), "-2.5");
    }

    TYPED_TEST(NumberFloatingPointTest, ParsesDecimalAndScientificNotation)
    {
        EXPECT_EQ(Number("1.25").template as<TypeParam>(), TypeParam{1.25});
        EXPECT_EQ(Number("-2.5").template as<TypeParam>(), TypeParam{-2.5});
        EXPECT_EQ(Number("6.25e2").template as<TypeParam>(), TypeParam{625});
    }

    TYPED_TEST(NumberFloatingPointTest, RoundTripsHighPrecisionValues)
    {
        const TypeParam values[] = {std::nextafter(TypeParam{1}, TypeParam{2}),
                                    std::numeric_limits<TypeParam>::denorm_min(), std::numeric_limits<TypeParam>::min(),
                                    std::numeric_limits<TypeParam>::max()};
        for (const TypeParam value : values)
        {
            const Number number(value);
            SCOPED_TRACE(number.raw());

            EXPECT_EQ(number.template as<TypeParam>(), value);
        }
    }

    TYPED_TEST(NumberFloatingPointTest, SupportsImplicitConversion)
    {
        const Number number("1.25");
        const TypeParam value = number;

        EXPECT_EQ(value, TypeParam{1.25});
    }

    TYPED_TEST(NumberFloatingPointTest, RejectsNonFiniteConstruction)
    {
        EXPECT_THROW(static_cast<void>(Number(std::numeric_limits<TypeParam>::infinity())), std::invalid_argument);
        EXPECT_THROW(static_cast<void>(Number(-std::numeric_limits<TypeParam>::infinity())), std::invalid_argument);
        EXPECT_THROW(static_cast<void>(Number(std::numeric_limits<TypeParam>::quiet_NaN())), std::invalid_argument);
    }

    TYPED_TEST(NumberFloatingPointTest, RejectsOverflowAndUnderflow)
    {
        const Number positive("1e" + std::to_string(std::numeric_limits<TypeParam>::max_exponent10 + 1));
        const Number negative("-" + positive.raw());
        const Number tiny("1e-100000");

        EXPECT_THROW(positive.template as<TypeParam>(), std::runtime_error);
        EXPECT_THROW(negative.template as<TypeParam>(), std::runtime_error);
        EXPECT_THROW(tiny.template as<TypeParam>(), std::runtime_error);
        EXPECT_THROW(static_cast<void>(static_cast<TypeParam>(positive)), std::runtime_error);
    }

    TYPED_TEST(NumberFloatingPointTest, PreservesSignedZero)
    {
        const Number negative(TypeParam{-0.0});
        const TypeParam value = negative.template as<TypeParam>();

        EXPECT_EQ(value, TypeParam{0});
        EXPECT_TRUE(std::signbit(value));
        EXPECT_TRUE(std::signbit(Number("-0e99999999999999999999999").template as<TypeParam>()));
    }

    template <typename T>
    concept CanReadNumber = requires(const Number& number) { number.template as<T>(); };

    TEST(NumberConversionTest, SupportsNumericTypesOnly)
    {
        static_assert(!std::is_constructible_v<Number, bool>);
        static_assert(!std::is_convertible_v<Number, bool>);
        static_assert(!CanReadNumber<bool>);
        static_assert(!CanReadNumber<const bool>);
        static_assert(!CanReadNumber<std::string>);
        static_assert(CanReadNumber<long double>);

        EXPECT_EQ(Number(char{65}).raw(), "65");
        EXPECT_EQ(Number(42L).as<long>(), 42L);
        EXPECT_EQ(Number(42UL).as<unsigned long>(), 42UL);
    }

    TEST(NumberConversionTest, ReportsConversionFailure)
    {
        try
        {
            static_cast<void>(Number("1e100000").as<double>());
            FAIL() << "Expected a conversion error";
        }
        catch (const std::runtime_error& error)
        {
            EXPECT_STREQ(error.what(), "Number conversion failed");
        }
    }

    TEST(NumberConversionTest, HandlesMovedFromValuesWithoutInvalidAccess)
    {
        Number source("42");
        const Number destination(std::move(source));

        EXPECT_EQ(destination.as<int>(), 42);
        if (source.raw().empty())
        {
            EXPECT_THROW(source.as<int>(), std::runtime_error);
            EXPECT_THROW(source.as<double>(), std::runtime_error);
        }
    }
} // namespace
