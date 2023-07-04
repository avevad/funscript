#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers_string.hpp"

#include "tests.hpp"

#define EVALUATES_TO(...) EvaluatesTo(env, ##__VA_ARGS__)
#define PANICS Panics(env)
#define EVALUATES Evaluates(env)

// Note: later tests can rely on features tested in earlier ones

using namespace funscript;
using namespace funscript::tests;

TEST_CASE("Integers", "[integers]") {
    TestEnv env;
    SECTION("Arithmetic") {
        CHECK_THAT("(2 + 3) * 2", EVALUATES_TO(10));
        CHECK_THAT("234 / 100, 234 % 100", EVALUATES_TO(2, 34));
        CHECK_THAT("-(2 * 2)", EVALUATES_TO(-4));
    };
    SECTION("Comparisons") {
        CHECK_THAT("50 > 10, 50 < 10", EVALUATES_TO(true, false));
        CHECK_THAT("21 != 21, 21 == 21", EVALUATES_TO(false, true));
        CHECK_THAT("-3 <= 10, -7 >= -7", EVALUATES_TO(true, true));
    };
    SECTION("Invalid operations") {
        CHECK_THAT("1 / 0", PANICS);
        CHECK_THAT("0 / 0", PANICS);
        CHECK_THAT("/ 5", PANICS);
        CHECK_THAT("* 3", PANICS);
        CHECK_THAT("(1, 3) + (2, 4)", PANICS);
        CHECK_THAT("2-", PANICS);
    }
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
        CHECK_THAT("2. + 1", PANICS);
        CHECK_THAT("5 / 2.", PANICS);
        CHECK_THAT("0. > 1", PANICS);
    };
}

TEST_CASE("Variables and scopes", "[scopes]") {
    TestEnv env;
    SECTION("Declaration") {
        REQUIRE_THAT("new_var = 1", PANICS);
        REQUIRE_THAT(".new_var = 1", EVALUATES);
        CHECK_THAT("new_var == 1", EVALUATES_TO(true));
        CHECK_THAT(".new_var", EVALUATES_TO(1));
    };
    SECTION("Shadowing") {
        REQUIRE_THAT(".var = 1", EVALUATES);
        CHECK_THAT("(.var = 2; var)", EVALUATES_TO(2));
        CHECK_THAT("var", EVALUATES_TO(1));
    };
    SECTION("Lifetime") {
        REQUIRE_THAT("(.var = 1)", EVALUATES);
        CHECK_THAT("var", PANICS);
    };
}

TEST_CASE("Assignment expressions", "[assignments]") {
    TestEnv env;
    SECTION("Multiple assignment") {
        REQUIRE_THAT(".a, .b = 1, 2", EVALUATES);
        CHECK_THAT("a, b", EVALUATES_TO(1, 2));
    };
    SECTION("Swapping") {
        REQUIRE_THAT(".foo, .bar = yes, no", EVALUATES);
        REQUIRE_THAT("foo, bar = bar, foo", EVALUATES);
        CHECK_THAT("foo, bar", EVALUATES_TO(false, true));
    };
    SECTION("Underflow") {
        REQUIRE_THAT(".var1, .var2 = 123, 456", EVALUATES);
        REQUIRE_THAT(".var3, .var4, .var5 = var1, var2", PANICS);
        CHECK_THAT("var3 == var1", EVALUATES_TO(true));
        CHECK_THAT("var4 == var2", EVALUATES_TO(true));
    };
    SECTION("Overflow") {
        REQUIRE_THAT(".lorem, .ipsum = 'lorem', 'ipsum', 'dolor', 'sit', 'amet'", PANICS);
        CHECK_THAT("lorem, ipsum", EVALUATES_TO("lorem", "ipsum"));
    };
}

TEST_CASE("Conditionals", "[conditionals]") {
    TestEnv env;
    SECTION("Conditional operator") {
        REQUIRE_THAT(".answer = 42", EVALUATES);
        CHECK_THAT("answer == 32 then 'it cannot be'", EVALUATES_TO());
        CHECK_THAT("answer == 42 then 'of course it is'", EVALUATES_TO("of course it is"));
        CHECK_THAT("not (answer < 0) then 'must be so'", EVALUATES_TO("must be so"));
    };
    SECTION("Else clause") {
        REQUIRE_THAT(".val1, .val2 = 54, 35", EVALUATES);
        CHECK_THAT("val1 > val2 then val1 else val2", EVALUATES_TO(54));
        CHECK_THAT("val1 == val2 then 'same' else 'different'", EVALUATES_TO("different"));
    }
}

TEST_CASE("Functions", "[functions]") {
    TestEnv env;
    SECTION("Creation and calling") {
        REQUIRE_THAT(".sum = (.a, .b) -> a + b", EVALUATES);
        CHECK_THAT("sum(13, 27)", EVALUATES_TO(13 + 27));
        CHECK_THAT("a", PANICS);
        CHECK_THAT("b", PANICS);
        REQUIRE_THAT(".divmod = (.a, .b) -> (a / b, a % b)", EVALUATES);
        CHECK_THAT("divmod(32, 10)", EVALUATES_TO(3, 2));
    };
    SECTION("Arguments") {
        REQUIRE_THAT(".sum3 = (.a, .b, .c) -> a + b + c", EVALUATES);
        CHECK_THAT("sum3(1, 2, 3, 4)", PANICS);
        CHECK_THAT("sum3(1, 10, 15)", EVALUATES_TO(1 + 10 + 15));
        CHECK_THAT("sum3(1, 5)", PANICS);
        CHECK_THAT("sum3()", PANICS);
    };
    SECTION("Multiple return") {
        REQUIRE_THAT(".sum5 = (.a, .b, .c, .d, .e) -> a + b + c + d + e", EVALUATES);
        REQUIRE_THAT(".plus_minus = .n -> (n - 1, n + 1)", EVALUATES);
        CHECK_THAT("plus_minus 5", EVALUATES_TO(4, 6));
        CHECK_THAT("sum5(plus_minus 2, 5, plus_minus 8)", EVALUATES_TO(1 + 3 + 5 + 7 + 9));
    };
    SECTION("Recursion") {
        REQUIRE_THAT(".factorial = .n -> (n == 0 then 1 else factorial(n - 1) * n)", EVALUATES);
        CHECK_THAT("factorial 10 == 3628800", EVALUATES_TO(true));
        REQUIRE_THAT(".f = -> f()", EVALUATES);
        CHECK_THAT("f()", PANICS);
    }
}

TEST_CASE("Strings", "[strings]") {
    TestEnv env;
    SECTION("Creation") {
        REQUIRE_THAT(".empty = ''", EVALUATES);
        REQUIRE_THAT(".some = 'some str'", EVALUATES);
        CHECK_THAT("some, empty", EVALUATES_TO("some str", ""));
    };
    SECTION("Concatenation") {
        REQUIRE_THAT(".str1 = 'impostor'", EVALUATES);
        REQUIRE_THAT(".str2 = 'is sus'", EVALUATES);
        CHECK_THAT("str1 + ' ' + str2", EVALUATES_TO("impostor is sus"));
    };
    SECTION("Invalid operations") {
        CHECK_THAT("() + ''", PANICS);
        CHECK_THAT("'I am ' + 17 + ' years old'", PANICS);
        CHECK_THAT("'Can drive: ' + no", PANICS);
        CHECK_THAT("('', '') + ('a', 'b', 'c')", PANICS);
        CHECK_THAT("'That is not' 'how it works'", PANICS);
    };
}

TEST_CASE("Loops", "[loops]") {
    TestEnv env;
    SECTION("Pre-condition loop") {
        CHECK_THAT(".i = 0; i != 5 repeats (i, (i = i + 1))", EVALUATES_TO(0, 1, 2, 3, 4));
        CHECK_THAT("(1 == 0) repeats (5)", EVALUATES_TO());
        CHECK_THAT("yes repeats 1", PANICS); // Stack overflow
    };
    SECTION("Post-condition loop") {
        CHECK_THAT(".i = 0; (i = i + 1; i) until i == 7", EVALUATES_TO(1, 2, 3, 4, 5, 6, 7));
        CHECK_THAT("'some str' until 2 * 2 == 4", EVALUATES_TO("some str"));
        CHECK_THAT("1 until no", PANICS); // Stack overflow
    };
}

TEST_CASE("Arrays", "[arrays]") {
    TestEnv env;
    SECTION("Creation") {
        REQUIRE_THAT(".five_nums = [11, 12, 13, 14, 15]", EVALUATES);
        REQUIRE_THAT(".empty_arr = []", EVALUATES);
        REQUIRE_THAT(".my_str = 'some_string'", EVALUATES);
        REQUIRE_THAT(".stuff = [0, (.x -> x + 1), 5., my_str, no]", EVALUATES);
    };
    SECTION("Element access") {
        REQUIRE_THAT(".three_nums = [11, 12, 13]", EVALUATES);
        CHECK_THAT("three_nums[2]", EVALUATES_TO(13));
        CHECK_THAT("three_nums[-1]", PANICS);
        CHECK_THAT("three_nums[[]]", PANICS);
        REQUIRE_THAT(".stuff = ['str', 0, (->), yes, no, 5]", EVALUATES);
        REQUIRE_THAT(".num1, .num2, .bln, .str = stuff[5, 1, 3, 0]", EVALUATES);
        CHECK_THAT("str, num2, bln", EVALUATES_TO("str", 0, true));
    };
    SECTION("Modification") {
        REQUIRE_THAT(".values = ['test', -1, 3, 3, 7, ]", EVALUATES);
        REQUIRE_THAT("values[1] = values", EVALUATES);
        CHECK_THAT("values[1][1][1][1][1][1] is values", EVALUATES_TO(true));
        CHECK_THAT("values[-1] = no", PANICS);
        CHECK_THAT("values[5] = yes", PANICS);
        REQUIRE_THAT("values[0, 1, 2, 3, 4] = 'a', 'b', 'c', 'd', 'e'", EVALUATES);
        CHECK_THAT("values[3]", EVALUATES_TO("d"));
    };
    SECTION("Generation") {
        REQUIRE_THAT(".ten_nums = [.i = 0; (i = i + 1; i) until i == 10]", EVALUATES);
        REQUIRE_THAT("ten_nums[7] == 8", EVALUATES_TO(true));
    };
    SECTION("Concatenation") {
        REQUIRE_THAT(".alpha1 = ['a', 'b', 'c']", EVALUATES);
        REQUIRE_THAT(".alpha2 = ['d', 'e', 'f', 'g', 'h']", EVALUATES);
        REQUIRE_THAT("(alpha1 + alpha2)[6, 5, 1]", EVALUATES_TO("g", "f", "b"));
    };
    SECTION("Multiplication") {
        REQUIRE_THAT(".arr = ['test', no, 5]", EVALUATES);
        CHECK_THAT("(arr * 5)[7]", EVALUATES_TO(false));
        CHECK_THAT("(9 * arr)[9 * 3 - 1]", EVALUATES_TO(5));
    };
}

TEST_CASE("Objects", "[objects]") {
    TestEnv env;
    SECTION("Creation") {
        CHECK_THAT("{}", EVALUATES);
        CHECK_THAT("{.str = 'a'; .int = 2; .bln = yes; }", EVALUATES);
        CHECK_THAT("{1, 2, 'some str', yes}", EVALUATES);
        CHECK_THAT("{.err = yes; 'unknown error'}", EVALUATES);
    };
    SECTION("Field access") {
        REQUIRE_THAT(".person = {.name = 'John'; .age = 31; .male = yes; }", EVALUATES);
        CHECK_THAT("person.name", EVALUATES_TO("John"));
        CHECK_THAT("person.friends", PANICS);
    };
    SECTION("Field modification") {
        REQUIRE_THAT(".dog = {.name = 'Bailey'; .breed = 'Golden retriever'; .age = 4}", EVALUATES);
        REQUIRE_THAT("dog.age = dog.age + 1 # Happy B-Day, Bailey", EVALUATES);
        CHECK_THAT("dog.age < 5", EVALUATES_TO(false));
    };
    SECTION("Methods") {
        REQUIRE_THAT(".Counter = .val -> {.value = -> val; .inc = -> (val = val + 1); .dec = -> (val = val - 1); }",
                     EVALUATES);
        REQUIRE_THAT(".cnt = Counter(5)", EVALUATES);
        REQUIRE_THAT("cnt.value()", EVALUATES_TO(5));
        REQUIRE_THAT("cnt.inc(); cnt.inc(); cnt.dec();", EVALUATES);
        CHECK_THAT("cnt.value()", EVALUATES_TO(6));
    };
    SECTION("Result unwrapping") {
        REQUIRE_THAT(".panic = -> 0 / 0", EVALUATES);
        CHECK_THAT("{1, 2, 3, no, yes, 'sus'}?", EVALUATES_TO(1, 2, 3, false, true, "sus"));
        CHECK_THAT("{.err = yes; {}, [], {{}}} ? panic()", PANICS);
        REQUIRE_THAT(".fail = yes", EVALUATES);
        REQUIRE_THAT(".get_str = -> (fail then {.err = yes} else {'avevad'})", EVALUATES);
        REQUIRE_THAT(".display_username = -> {'The username is: ' + get_str()?}", EVALUATES);
        CHECK_THAT("display_username() ? panic()", PANICS);
        REQUIRE_THAT(".fail = no", EVALUATES);
        CHECK_THAT("display_username() ? panic()", EVALUATES_TO("The username is: avevad"));
    };
    SECTION("Typechecking") {
        REQUIRE_THAT(".int = {.check_value = .x -> x % 1}", EVALUATES);
        REQUIRE_THAT(".f = (.x: int, .y: int) -> int: x + y", EVALUATES);
        CHECK_THAT("f(12, 34)", EVALUATES_TO(12 + 34));
        CHECK_THAT("f('test', 'text')", PANICS);
        CHECK_THAT("f()", PANICS);
        CHECK_THAT("f(12, 34, 56)", PANICS);
        REQUIRE_THAT(".float = {.check_value = .x -> x + 0.}", EVALUATES);
        REQUIRE_THAT(".g = (.x: int, .y: float) -> (float, int): (y, x)", EVALUATES);
        CHECK_THAT("g(1, 0.5)", EVALUATES_TO(0.5, 1));
    }
}