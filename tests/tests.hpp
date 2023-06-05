#ifndef FUNSCRIPT_TESTS_HPP
#define FUNSCRIPT_TESTS_HPP

#include <utility>

#include "mm.hpp"
#include "vm.hpp"
#include "utils.hpp"
#include "catch2/matchers/catch_matchers_templated.hpp"

using namespace funscript;

template<typename T>
struct ValueChecker {
};

template<>
class ValueChecker<bool> {
    bool value_exp;
public:
    explicit ValueChecker(bool value_exp) : value_exp(value_exp) {}

    [[nodiscard]] bool check_value(const VM::Value &value) const {
        return value.type == Type::BLN && value.data.bln == value_exp;
    }
};

template<>
class ValueChecker<double> {
    double value_exp;
public:
    explicit ValueChecker(double value_exp) : value_exp(value_exp) {}

    [[nodiscard]] bool check_value(const VM::Value &value) const {
        return value.type == Type::FLP && value.data.flp == value_exp;
    }
};

template<>
class ValueChecker<int> {
    int value_exp;
public:
    explicit ValueChecker(int value_exp) : value_exp(value_exp) {}

    [[nodiscard]] bool check_value(const VM::Value &value) const {
        return value.type == Type::INT && value.data.num == value_exp;
    }
};

template<>
class ValueChecker<const char *> {
    std::string value_exp;
public:
    explicit ValueChecker(const char *value_exp) : value_exp(value_exp) {}

    [[nodiscard]] bool check_value(const VM::Value &value) const {
        return value.type == Type::STR && std::string(value.data.str->bytes) == value_exp;
    }
};

template<typename T>
bool check_value(const VM::Value &value, const T &value_exp) {
    return ValueChecker<T>(value_exp).check_value(value);
}

bool check_values(const VM::Stack &values, size_t pos) {
    return pos == values.size();
}

template<typename Values0>
bool check_values(const VM::Stack &values, size_t pos,
                  const Values0 &values_exp0) {
    if (pos + 1 != values.size()) return false;
    if (!check_value(values[pos], values_exp0)) return false; // NOLINT(cppcoreguidelines-narrowing-conversions)
    return true;
}

template<typename Values0, typename Values1, typename... Values>
bool check_values(const VM::Stack &values, size_t pos,
                  const Values0 &values_exp0, const Values1 &values_exp1, const Values &... values_exp) {
    if (pos >= values.size()) return false;
    if (!check_value(values[pos], values_exp0)) return false; // NOLINT(cppcoreguidelines-narrowing-conversions)
    return check_values(values, pos + 1, values_exp1, values_exp...);
}

static std::string extract_error_msg(VM::Error *err) {
    auto msg_val = err->obj->get_field(FStr("msg", err->obj->vm.mem.str_alloc()));
    if (msg_val.has_value() && msg_val.value().type == Type::STR) {
        return std::string(msg_val.value().data.str->bytes);
    }
    return "";
}

class EvaluationError : std::runtime_error {
public:
    explicit EvaluationError(VM::Error *err) : std::runtime_error(extract_error_msg(err)) {}
};

class TestEnv {
    DefaultAllocator allocator;
    VM vm;
    MemoryManager::AutoPtr<VM::Scope> scope;
public:
    explicit TestEnv(
            size_t memory_max_bytes = 1073741824 /* 1 GiB */,
            size_t frames_max = 1024 /* 1 Ki */,
            size_t values_max = 67108864 /* 64 Mi */
    ) : allocator(memory_max_bytes),
        vm({.mm{.allocator = &allocator}, .stack_values_max = values_max, .stack_frames_max = frames_max}),
        scope(vm.mem.gc_new_auto<VM::Scope>(vm.mem.gc_new_auto<VM::Object>(vm).get(), nullptr)) {
    }

    auto evaluate(const std::string &expr) {
        auto stack = util::eval_expr(vm, scope.get(), "<test>", expr);
        if (stack->size() != 0 && (*stack)[0].type == Type::ERR) {
            throw EvaluationError((*stack)[0].data.err);
        }
        return stack;
    }
};


template<typename... Values>
struct EvaluatesTo : Catch::Matchers::MatcherGenericBase {
    std::tuple<Values...> values_exp;
    TestEnv &env;

    explicit EvaluatesTo(TestEnv &env, Values... values_exp) :
            env(env), values_exp(std::move(values_exp)...) {}

    bool match(const std::string &expr) const {
        return std::apply([this, &expr](const Values &... values_exp_pack) -> bool {
            try {
                auto stack = env.evaluate(expr);
                return check_values(*stack, 0, values_exp_pack...);
            } catch (const EvaluationError &err) {
                return false;
            }
        }, values_exp);
    }

    std::string describe() const override {
        return "evaluates to specified values";
    }
};

struct Fails : Catch::Matchers::MatcherGenericBase {
    TestEnv &env;

    explicit Fails(TestEnv &env) : env(env) {}

    bool match(const std::string &expr) const {
        try {
            auto stack = env.evaluate(expr);
            return false;
        } catch (const EvaluationError &err) {
            return true;
        }
    }

    std::string describe() const override {
        return "fails";
    }
};

struct Succeeds : Catch::Matchers::MatcherGenericBase {
    TestEnv &env;

    explicit Succeeds(TestEnv &env) : env(env) {}

    bool match(const std::string &expr) const {
        try {
            auto stack = env.evaluate(expr);
            return true;
        } catch (const EvaluationError &err) {
            return false;
        }
    }

    std::string describe() const override {
        return "succeeds";
    }
};

#endif //FUNSCRIPT_TESTS_HPP
