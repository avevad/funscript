#ifndef FUNSCRIPT_NATIVE_HPP
#define FUNSCRIPT_NATIVE_HPP

#include <variant>
#include "utils.hpp"

namespace funscript::native {

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
    static void
    call_native_function(VM &vm, VM::Stack &stack, VM::Frame *frame, const std::function<Ret(Args...)> &fn) {
        auto frame_start = stack.find_sep();
        stack.reverse();
        try {
            auto args = util::values_from_stack<Args...>(vm, stack);
            auto ret = std::apply(fn, std::move(args));
            stack.pop(frame_start);
            util::value_to_stack(vm, stack, ret);
        } catch (const NativeError &err) {
            return stack.raise_err(err.what(), frame_start);
        } catch (const util::ValueError &err) {
            return stack.raise_err(err.what(), frame_start);
        }
    }

}

#endif //FUNSCRIPT_NATIVE_HPP
