#ifndef FUNSCRIPT_TESTS_HPP
#define FUNSCRIPT_TESTS_HPP

#include "mm.hpp"
#include "vm.hpp"
#include "utils.hpp"
#include "catch2/matchers/catch_matchers_templated.hpp"

#include <utility>

namespace funscript::tests {

    namespace {

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

    }

    template<typename T>
    static bool check_value(const VM::Value &value, const T &value_exp) {
        return ValueChecker<T>(value_exp).check_value(value);
    }


    namespace {

        template<typename... Values, size_t... Indices>
        bool check_values_impl(const VM::Stack &values, const std::tuple<Values...> &values_exp,
                               std::index_sequence<Indices...>) {
            return values.size() == sizeof...(Values) &&
                   (check_value(values[Indices], get<Indices>(values_exp)) && ...);
        }
    }

    template<typename... Values>
    static bool check_values(const VM::Stack &values, const Values &... values_exp) {
        return check_values_impl(values, std::tuple<const Values &...>(values_exp...),
                                 std::index_sequence_for<Values...>());
    }

    class EvaluationError : std::runtime_error {
    public:
        explicit EvaluationError(const std::string &msg) : std::runtime_error(msg) {}
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
            auto stack = util::eval_expr(vm, nullptr, scope.get(), "<test>", expr, "'<test>'");
            if (stack->is_panicked()) {
                throw EvaluationError(std::string((*stack)[-1].data.str->bytes));
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
                    return check_values(*stack, values_exp_pack...);
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

}

#endif //FUNSCRIPT_TESTS_HPP