#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers_string.hpp"

#include "tests.hpp"

#define EVALUATES_TO(...) EvaluatesTo(env, ##__VA_ARGS__)
#define FAILS Fails(env)
#define SUCCEEDS Succeeds(env)

// Note: later tests can use features tested in earlier ones

TEST_CASE("Integer expressions", "[expr_int]") {
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

TEST_CASE("Floating point expressions", "[expr_flp]") {
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
    CHECK_THAT("var = 1", FAILS);
    REQUIRE_THAT(".var = 1", SUCCEEDS);
    REQUIRE_THAT("var", EVALUATES_TO(1));
    CHECK_THAT("(.var2 = 1; var)", EVALUATES_TO(1));
    CHECK_THAT("var2", FAILS);
    CHECK_THAT("(.var = 2; var)", EVALUATES_TO(2));
    CHECK_THAT("var", EVALUATES_TO(1));
}

TEST_CASE("Functions", "[functions]") {
    TestEnv env;
    REQUIRE_THAT(".sum = (.a, .b): a + b", SUCCEEDS);
    CHECK_THAT("sum(13, 27)", EVALUATES_TO(13 + 27));
    REQUIRE_THAT(".sum3 = (.a, .b, .c): sum(a, b) + c", SUCCEEDS);
    CHECK_THAT("sum3(1, 10, 15)", EVALUATES_TO(1 + 10 + 15));
    CHECK_THAT("sum3(1, 5)", FAILS);
    CHECK_THAT("sum3()", FAILS);
    CHECK_THAT("sum3(1, 2, 3, 4)", FAILS);
    REQUIRE_THAT(".plus_minus = .n: (n - 1, n + 1)", SUCCEEDS);
    CHECK_THAT("plus_minus 5", EVALUATES_TO(4, 6));
    CHECK_THAT("plus_minus(5, 6)", FAILS);
    CHECK_THAT("sum3(1, plus_minus 9)", EVALUATES_TO(1 + 8 + 10));
    CHECK_THAT("a", FAILS);
    CHECK_THAT("n", FAILS);
}

TEST_CASE("Conditionals", "[conditionals]") {
    TestEnv env;
    REQUIRE_THAT(".max = (.a, .b): (a > b then a else b)", SUCCEEDS);
    REQUIRE_THAT(".val1 = 349 * 512", SUCCEEDS);
    REQUIRE_THAT(".val2 = 237 * 601", SUCCEEDS);
    CHECK_THAT("max(val1, val2)", EVALUATES_TO(std::max(349 * 512, 237 * 601)));
    REQUIRE_THAT(".filter = .pred: .val: (pred(val) then val)", SUCCEEDS);
    REQUIRE_THAT(".not7 = .val: val != 7", SUCCEEDS);
    CHECK_THAT("filter(not7)(5)", EVALUATES_TO(5));
    CHECK_THAT("filter(not7)(7)", EVALUATES_TO());
}

TEST_CASE("Assignment expressions", "[assignments]") {
    TestEnv env;
    REQUIRE_THAT(".a, .b = 1, 2", SUCCEEDS);
    REQUIRE_THAT("a, b", EVALUATES_TO(1, 2));
    REQUIRE_THAT("a, b = b, a", SUCCEEDS);
    REQUIRE_THAT("a, b", EVALUATES_TO(2, 1));
    REQUIRE_THAT(".x, .y, .z = a, b", FAILS);
    CHECK_THAT("x, y", EVALUATES_TO(2, 1));
    REQUIRE_THAT("a, b, x = x, a, b, c", FAILS);
}

TEST_CASE("Strings", "[strings]") {
    TestEnv env;
    REQUIRE_THAT(".str1 = 'amogus'", SUCCEEDS);
    REQUIRE_THAT(".str2 = 'is sus'", SUCCEEDS);
    CHECK_THAT("str1 + ' ' + str2", EVALUATES_TO("amogus is sus"));
}

TEST_CASE("Loops", "[loops]") {
    TestEnv env;
    SECTION("Precondition loop") {
        REQUIRE_THAT(".i = 0; i != 5 do (i, (i = i + 1))", EVALUATES_TO(0, 1, 2, 3, 4));
        REQUIRE_THAT("(1 == 0) do (5)", EVALUATES_TO());
    };
    SECTION("Postcondition loop") {
        REQUIRE_THAT(".i = 0; (i = i + 1; i) until i == 7", EVALUATES_TO(1, 2, 3, 4, 5, 6, 7));
        REQUIRE_THAT("'some str' until 2 * 2 == 4", EVALUATES_TO("some str"));
    }
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
    }
}