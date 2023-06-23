#ifndef FUNSCRIPT_NATIVE_HPP
#define FUNSCRIPT_NATIVE_HPP

#include "utils.hpp"

#include <variant>

namespace funscript::native {

    /**
     * Helper function that allows to transform stack values into C++ values and pass them as arguments to any function.
     * It also transforms return value of the C++ function into Funscript values and pushes it onto the stack.
     * @tparam Ret Return type of the C++ function.
     * @tparam Args Argument types of the C++ function.
     * @param stack Execution stack to operate with.
     * @param frame Current frame of execution stack.
     * @param fn The C++ function itself.
     */
    template<typename Ret, typename... Args>
    static void
    call_function(VM &vm, VM::Stack &stack, const std::function<Ret(Args...)> &fn) {
        auto frame_start = stack.find_sep();
        stack.reverse();
        try {
            auto args = util::values_from_stack<Args...>(vm, stack);
            auto ret = std::apply(fn, std::move(args));
            stack.pop(frame_start);
            util::value_to_stack(vm, stack, ret);
        } catch (const util::ValueError &err) {
            stack.panic(err.what());
        }
    }

}

#endif //FUNSCRIPT_NATIVE_HPP
