#ifndef FUNSCRIPT_NATIVE_HPP
#define FUNSCRIPT_NATIVE_HPP

#include <variant>

namespace funscript {

    template<typename T>
    struct ValueTransformer {

    };

    template<>
    struct ValueTransformer<fint> {
        static std::optional<fint> from_stack(VM &vm, VM::Stack &stack) {
            if (stack[-1].type != Type::INT) return std::nullopt;
            fint result = stack[-1].data.num;
            stack.pop();
            return result;
        }

        static void to_stack(VM &vm, VM::Stack &stack, fint num) {
            stack.push_int(num);
        }
    };

    template<>
    struct ValueTransformer<MemoryManager::AutoPtr<VM::Array>> {
        static std::optional<MemoryManager::AutoPtr<VM::Array>> from_stack(VM &vm, VM::Stack &stack) {
            if (stack[-1].type != Type::ARR) return std::nullopt;
            auto result = MemoryManager::AutoPtr(vm.mem, stack[-1].data.arr);
            stack.pop();
            return result;
        }

        static void to_stack(VM &vm, VM::Stack &stack, const MemoryManager::AutoPtr<VM::Array> &arr) {
            stack.push_arr(arr.get());
        }
    };

    template<typename T>
    std::optional<T> value_from_stack(VM &vm, VM::Stack &stack) {
        return ValueTransformer<T>::from_stack(vm, stack);
    }

    template<typename T>
    void value_to_stack(VM &vm, VM::Stack &stack, const T &val) {
        ValueTransformer<T>::to_stack(vm, stack, val);
    }

    std::variant<std::tuple<>, std::string> values_from_stack(VM &vm, VM::Stack &stack, size_t pos) {
        if (stack[-1].type != Type::SEP) {
            return "too many values, required " + std::to_string(pos);
        }
        return {};
    }

    template<typename Values0>
    std::variant<std::tuple<Values0>, std::string> values_from_stack(VM &vm, VM::Stack &stack, size_t pos) {
        auto val = value_from_stack<Values0>(vm, stack);
        if (!val.has_value()) return "value #" + std::to_string(pos + 1) + " is absent or is of wrong type";
        auto vals = values_from_stack(vm, stack, pos + 1);
        if (holds_alternative<std::string>(vals)) {
            return std::get<std::string>(vals);
        }
        return {val.value()};
    }

    template<typename Values0, typename Values1, typename... Values>
    std::variant<std::tuple<Values0, Values1, Values...>, std::string>
    values_from_stack(VM &vm, VM::Stack &stack, size_t pos) {
        auto val = value_from_stack<Values0>(vm, stack);
        if (!val.has_value()) return "value #" + std::to_string(pos + 1) + " is absent or is of wrong type";
        auto vals = values_from_stack<Values1, Values...>(vm, stack, pos + 1);
        if (holds_alternative<std::string>(vals)) {
            return std::get<std::string>(vals);
        }
        return std::tuple_cat(std::tuple<Values0>(std::move(val.value())),
                              std::move(std::get<std::tuple<Values1, Values...>>(vals)));
    }

    class NativeError : public std::runtime_error {
    public:
        explicit NativeError(const std::string &what) : std::runtime_error(what) {}
    };

    /**
     * Helper function that allows to transform stack values into C++ values and pass them as arguments to any function.
     * It also transforms return value of the C++ function into funscript values and pushes it onto the stack.
     * @tparam Ret Return type of the C++ function.
     * @tparam Args Argument types of the C++ function.
     * @param stack Execution stack to operate with.
     * @param frame Current frame of execution stack.
     * @param fn The C++ function itself.
     */
    template<typename Ret, typename... Args>
    void call_native_function(VM &vm, VM::Stack &stack, VM::Frame *frame, const std::function<Ret(Args...)> &fn) {
        auto frame_start = stack.find_sep();
        stack.reverse();
        auto args = values_from_stack<Args...>(vm, stack, 0);
        if (std::holds_alternative<std::string>(args)) {
            return stack.raise_err(std::get<std::string>(args), frame_start);
        }
        try {
            auto ret = std::apply(fn, std::move(std::get<std::tuple<Args...>>(args)));
            stack.pop(frame_start);
            value_to_stack(vm, stack, ret);
        } catch (const NativeError &err) {
            return stack.raise_err(err.what(), frame_start);
        }
    }
}

#endif //FUNSCRIPT_NATIVE_HPP
