#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "tests.h"

#define EVALUATES_TO(...) EvaluatesTo(env, ##__VA_ARGS__)
#define FAILS Fails(env)
#define SUCCEEDS Succeeds(env)

TEST_CASE("Integer expressions", "[expr_int]") {
    TestEnv env;
    CHECK_THAT("(2 + 3) * 2", EVALUATES_TO(10));
    CHECK_THAT("234 / 100, 234 % 100", EVALUATES_TO(2, 34));
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
    CHECK_THAT("sum(13, 27)", EVALUATES_TO(13 +27));
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