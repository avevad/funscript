#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers_string.hpp"

#include "tests.hpp"

#define EVALUATES_TO(...) EvaluatesTo(env, ##__VA_ARGS__)
#define FAILS Fails(env)
#define SUCCEEDS Succeeds(env)

// Note: later tests can use features tested in earlier ones

TEST_CASE("Integers", "[integers]") {
    TestEnv env;
    SECTION("Arithmetic") {
        CHECK_THAT("(2 + 3) * 2", EVALUATES_TO(10));
        CHECK_THAT("234 / 100, 234 % 100", EVALUATES_TO(2, 34));
        CHECK_THAT("1 / 0", FAILS);
    };
    SECTION("Comparisons") {
        CHECK_THAT("50 > 10, 50 < 10", EVALUATES_TO(true, false));
        CHECK_THAT("21 != 21, 21 == 21", EVALUATES_TO(false, true));
        CHECK_THAT("-3 <= 10, -7 >= -7", EVALUATES_TO(true, true));
    };
}

TEST_CASE("Floating point numbers", "[floats]") {
    TestEnv env;
    SECTION("Arithmetic") {
        CHECK_THAT("5. / 2., .5 * 2.", EVALUATES_TO(2.5, 1.));
        CHECK_THAT("1. + 2., 1. - 2.", EVALUATES_TO(3., -1.));
        CHECK_THAT("5. / 0.", EVALUATES_TO(inf()));
    };
    SECTION("Comparisons") {
        CHECK_THAT("-10. < 5., 10. > 5.", EVALUATES_TO(true, true));
        CHECK_THAT("inf <= 1000000., inf >= 0.", EVALUATES_TO(false, true));
    };
    SECTION("Type mixing") {
        CHECK_THAT("2. + 1", FAILS);
        CHECK_THAT("5 / 2.", FAILS);
        CHECK_THAT("0. > 1", FAILS);
    };
}

TEST_CASE("Variables and scopes", "[scopes]") {
    TestEnv env;
    SECTION("Declaration") {
        REQUIRE_THAT("newvar = 1", FAILS);
        REQUIRE_THAT(".newvar = 1", SUCCEEDS);
        CHECK_THAT("newvar == 1", EVALUATES_TO(true));
    };
    SECTION("Shadowing") {
        REQUIRE_THAT(".var = 1", SUCCEEDS);
        CHECK_THAT("(.var = 2; var)", EVALUATES_TO(2));
        CHECK_THAT("var", EVALUATES_TO(1));
    };
    SECTION("Lifetime") {
        REQUIRE_THAT("(.var = 1)", SUCCEEDS);
        CHECK_THAT("var", FAILS);
    };
}

TEST_CASE("Assignment expressions", "[assignments]") {
    TestEnv env;
    SECTION("Multiple assignment") {
        REQUIRE_THAT(".a, .b = 1, 2", SUCCEEDS);
        CHECK_THAT("a, b", EVALUATES_TO(1, 2));
    };
    SECTION("Swapping") {
        REQUIRE_THAT(".foo, .bar = yes, no", SUCCEEDS);
        REQUIRE_THAT("foo, bar = bar, foo", SUCCEEDS);
        CHECK_THAT("foo, bar", EVALUATES_TO(false, true));
    };
    SECTION("Underflow") {
        REQUIRE_THAT(".var1, .var2 = 123, 456", SUCCEEDS);
        REQUIRE_THAT(".var3, .var4, .var5 = var1, var2", FAILS);
        CHECK_THAT("var3 == var1", EVALUATES_TO(true));
        CHECK_THAT("var4 == var2", EVALUATES_TO(true));
    };
    SECTION("Overflow") {
        REQUIRE_THAT(".lorem, .ipsum = 'lorem', 'ipsum', 'dolor', 'sit', 'amet'", FAILS);
        CHECK_THAT("lorem, ipsum", EVALUATES_TO("lorem", "ipsum"));
    };
}

TEST_CASE("Conditionals", "[conditionals]") {
    TestEnv env;
    SECTION("Conditional operator") {
        REQUIRE_THAT(".answer = 42", SUCCEEDS);
        CHECK_THAT("answer == 32 then 'it cannot be'", EVALUATES_TO());
        CHECK_THAT("answer == 42 then 'of course it is'", EVALUATES_TO("of course it is"));
    };
    SECTION("Else clause") {
        REQUIRE_THAT(".val1, .val2 = 54, 35", SUCCEEDS);
        CHECK_THAT("val1 > val2 then val1 else val2", EVALUATES_TO(54));
        CHECK_THAT("val1 == val2 then 'same' else 'different'", EVALUATES_TO("different"));
    }
}

TEST_CASE("Functions", "[functions]") {
    TestEnv env;
    SECTION("Creation and calling") {
        REQUIRE_THAT(".sum = (.a, .b): a + b", SUCCEEDS);
        CHECK_THAT("sum(13, 27)", EVALUATES_TO(13 + 27));
        CHECK_THAT("a", FAILS);
        CHECK_THAT("b", FAILS);
    };
    SECTION("Arguments") {
        REQUIRE_THAT(".sum3 = (.a, .b, .c): a + b + c", SUCCEEDS);
        CHECK_THAT("sum3(1, 2, 3, 4)", FAILS);
        CHECK_THAT("sum3(1, 10, 15)", EVALUATES_TO(1 + 10 + 15));
        CHECK_THAT("sum3(1, 5)", FAILS);
        CHECK_THAT("sum3()", FAILS);
    };
    SECTION("Multiple return") {
        REQUIRE_THAT(".sum5 = (.a, .b, .c, .d, .e): a + b + c + d + e", SUCCEEDS);
        REQUIRE_THAT(".plus_minus = .n: (n - 1, n + 1)", SUCCEEDS);
        CHECK_THAT("plus_minus 5", EVALUATES_TO(4, 6));
        CHECK_THAT("sum5(plus_minus 2, 5, plus_minus 8)", EVALUATES_TO(1 + 3 + 5 + 7 + 9));
    };
}

TEST_CASE("Strings", "[strings]") {
    TestEnv env;
    SECTION("Creation") {
        REQUIRE_THAT(".empty = ''", SUCCEEDS);
        REQUIRE_THAT(".some = 'some str'", SUCCEEDS);
        CHECK_THAT("some, empty", EVALUATES_TO("some str", ""));
    };
    SECTION("Concatenation") {
        REQUIRE_THAT(".str1 = 'amogus'", SUCCEEDS);
        REQUIRE_THAT(".str2 = 'is sus'", SUCCEEDS);
        CHECK_THAT("str1 + ' ' + str2", EVALUATES_TO("amogus is sus"));
    };
}

TEST_CASE("Loops", "[loops]") {
    TestEnv env;
    SECTION("Pre-condition loop") {
        REQUIRE_THAT(".i = 0; i != 5 do (i, (i = i + 1))", EVALUATES_TO(0, 1, 2, 3, 4));
        REQUIRE_THAT("(1 == 0) do (5)", EVALUATES_TO());
    };
    SECTION("Post-condition loop") {
        REQUIRE_THAT(".i = 0; (i = i + 1; i) until i == 7", EVALUATES_TO(1, 2, 3, 4, 5, 6, 7));
        REQUIRE_THAT("'some str' until 2 * 2 == 4", EVALUATES_TO("some str"));
    };
}

TEST_CASE("Arrays", "[arrays]") {
    TestEnv env;
    SECTION("Creation") {
        REQUIRE_THAT(".five_nums = [11, 12, 13, 14, 15]", SUCCEEDS);
        REQUIRE_THAT(".empty_arr = []", SUCCEEDS);
        REQUIRE_THAT(".my_str = 'some_string'", SUCCEEDS);
        REQUIRE_THAT(".stuff = [0, nul, 5., my_str, no]", SUCCEEDS);
    };
    SECTION("Element access") {
        REQUIRE_THAT(".three_nums = [11, 12, 13]", SUCCEEDS);
        CHECK_THAT("three_nums[2]", EVALUATES_TO(13));
        CHECK_THAT("three_nums[-1]", FAILS);
        CHECK_THAT("three_nums[nul]", FAILS);
        REQUIRE_THAT(".stuff = ['str', 0, nul, yes, no, 5]", SUCCEEDS);
        REQUIRE_THAT(".num1, .num2, .bln, .str = stuff[5, 1, 3, 0]", SUCCEEDS);
        CHECK_THAT("str, num2, bln", EVALUATES_TO("str", 0, true));
    };
    SECTION("Generation") {
        REQUIRE_THAT(".ten_nums = [.i = 0; (i = i + 1; i) until i == 10]", SUCCEEDS);
        REQUIRE_THAT("ten_nums[7] == 8", EVALUATES_TO(true));
    };
    SECTION("Concatenation") {
        REQUIRE_THAT(".abc = ['a', 'b', 'c']", SUCCEEDS);
        REQUIRE_THAT(".defgh = ['d', 'e', 'f', 'g', 'h']", SUCCEEDS);
        REQUIRE_THAT("(abc + defgh)[6, 5, 1]", EVALUATES_TO("g", "f", "b"));
    };
    SECTION("Multiplication") {
        REQUIRE_THAT(".arr = ['test', no, 5]", SUCCEEDS);
        CHECK_THAT("(arr * 5)[7]", EVALUATES_TO(false));
        CHECK_THAT("(9 * arr)[9 * 3 - 1]", EVALUATES_TO(5));
    };
}