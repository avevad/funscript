#ifndef FUNSCRIPT_TESTS_H
#define FUNSCRIPT_TESTS_H

#include "../include/mm.h"
#include "../include/vm.h"
#include "../include/utils.h"
#include "catch2/matchers/catch_matchers_templated.hpp"

using namespace funscript;

bool check_value(const VM::Value &value, fint value_exp) {
    return value.type == Type::INT && value.data.num == value_exp;
}

bool check_value(const VM::Value &value, const std::string &value_exp) {
    return value.type == Type::STR && std::string(value.data.str->bytes) == value_exp;
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

class EvaluationError : std::runtime_error {
public:
    explicit EvaluationError(VM::Error *err) : std::runtime_error(std::string(err->desc)) {}
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
        auto stack = util::eval_expr(vm, scope.get(), expr);
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

#endif //FUNSCRIPT_TESTS_H
