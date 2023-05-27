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