#ifndef FUNSCRIPT_UTILS_HPP
#define FUNSCRIPT_UTILS_HPP

#include "tokenizer.hpp"
#include "ast.hpp"
#include "vm.hpp"

#include <iostream>
#include <fstream>
#include <dlfcn.h>
#include <filesystem>

namespace funscript::util {

    namespace {

        template<typename T>
        struct ValueTransformer {

        };

        template<>
        struct ValueTransformer<fint> {
            static std::optional<fint> from_stack(VM::Stack &stack) {
                if (stack[-1].type != Type::INT) return std::nullopt;
                fint result = stack[-1].data.num;
                stack.pop();
                return result;
            }

            static void to_stack(VM::Stack &stack, fint num) {
                stack.push_int(num);
            }
        };

        template<>
        struct ValueTransformer<fbln> {
            static std::optional<fbln> from_stack(VM::Stack &stack) {
                if (stack[-1].type != Type::BLN) return std::nullopt;
                fbln result = stack[-1].data.bln;
                stack.pop();
                return result;
            }

            static void to_stack(VM::Stack &stack, fbln bln) {
                stack.push_bln(bln);
            }
        };

        template<>
        struct ValueTransformer<MemoryManager::AutoPtr<VM::Array>> {
            static std::optional<MemoryManager::AutoPtr<VM::Array>> from_stack(VM::Stack &stack) {
                if (stack[-1].type != Type::ARR) return std::nullopt;
                auto result = MemoryManager::AutoPtr(stack[-1].data.arr);
                stack.pop();
                return result;
            }

            static void to_stack(VM::Stack &stack, const MemoryManager::AutoPtr<VM::Array> &arr) {
                stack.push_arr(arr.get());
            }
        };

        template<>
        struct ValueTransformer<MemoryManager::AutoPtr<VM::String>> {
            static std::optional<MemoryManager::AutoPtr<VM::String>> from_stack(VM::Stack &stack) {
                if (stack[-1].type != Type::STR) return std::nullopt;
                auto result = MemoryManager::AutoPtr(stack[-1].data.str);
                stack.pop();
                return result;
            }

            static void to_stack(VM::Stack &stack, const MemoryManager::AutoPtr<VM::String> &str) {
                stack.push_str(str.get());
            }
        };

        template<>
        struct ValueTransformer<MemoryManager::AutoPtr<Allocation>> {
            static std::optional<MemoryManager::AutoPtr<Allocation>> from_stack(VM::Stack &stack) {
                if (stack[-1].type != Type::PTR) return std::nullopt;
                auto result = MemoryManager::AutoPtr(stack[-1].data.ptr);
                stack.pop();
                return result;
            }

            static void to_stack(VM::Stack &stack, const MemoryManager::AutoPtr<Allocation> &ptr) {
                stack.push_ptr(ptr.get());
            }
        };

        template<>
        struct ValueTransformer<MemoryManager::AutoPtr<VM::Function>> {
            static std::optional<MemoryManager::AutoPtr<VM::Function>> from_stack(VM::Stack &stack) {
                if (stack[-1].type != Type::FUN) return std::nullopt;
                auto result = MemoryManager::AutoPtr(stack[-1].data.fun);
                stack.pop();
                return result;
            }

            static void to_stack(VM::Stack &stack, const MemoryManager::AutoPtr<VM::Function> &fun) {
                stack.push_fun(fun.get());
            }
        };

        template<>
        struct ValueTransformer<MemoryManager::AutoPtr<VM::Object>> {
            static std::optional<MemoryManager::AutoPtr<VM::Object>> from_stack(VM::Stack &stack) {
                if (stack[-1].type != Type::OBJ) return std::nullopt;
                auto result = MemoryManager::AutoPtr(stack[-1].data.obj);
                stack.pop();
                return result;
            }

            static void to_stack(VM::Stack &stack, const MemoryManager::AutoPtr<VM::Object> &obj) {
                stack.push_obj(obj.get());
            }
        };
    }

    template<typename T>
    static std::optional<T> value_from_stack(VM::Stack &stack) {
        return ValueTransformer<T>::from_stack(stack);
    }

    template<typename T>
    static void value_to_stack(VM::Stack &stack, const T &val) {
        ValueTransformer<T>::to_stack(stack, val);
    }

    class ValueError : public std::runtime_error {
    public:
        explicit ValueError(const std::string &what) : std::runtime_error(what) {}
    };

    namespace {

        std::tuple<> values_from_stack_impl(VM::Stack &stack, size_t pos) {
            if (stack[-1].type != Type::SEP) throw ValueError("too many values, required " + std::to_string(pos));
            stack.pop();
            return {};
        }

        template<typename Values0>
        std::tuple<Values0> values_from_stack_impl(VM::Stack &stack, size_t pos) {
            auto val = value_from_stack<Values0>(stack);
            if (!val.has_value()) {
                throw ValueError("value #" + std::to_string(pos + 1) + " is absent or is of wrong type");
            }
            values_from_stack_impl(stack, pos + 1);
            return {std::move(val.value())};
        }

        template<typename Values0, typename Values1, typename... Values>
        std::tuple<Values0, Values1, Values...>
        values_from_stack_impl(VM::Stack &stack, size_t pos) {
            auto val = value_from_stack<Values0>(stack);
            if (!val.has_value()) {
                throw ValueError("value #" + std::to_string(pos + 1) + " is absent or is of wrong type");
            }
            auto vals = values_from_stack_impl<Values1, Values...>(stack, pos + 1);
            return std::tuple_cat(std::tuple<Values0>(std::move(val.value())), std::move(vals));
        }

    }

    template<typename... Values>
    std::tuple<Values...> values_from_stack(VM::Stack &stack) {
        if constexpr (sizeof...(Values) == 0) {
            return values_from_stack_impl(stack, 0);
        } else {
            return values_from_stack_impl<Values...>(stack, 0);
        }
    }

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
    call_native_function(VM::Stack &stack, const std::function<Ret(Args...)> &fn) try {
        stack.reverse();
        if constexpr (std::is_same_v<Ret, void>) {
            std::apply([&fn](Args... args) -> Ret {
                return fn(std::move(args)...);
            }, util::values_from_stack<Args...>(stack));
        } else {
            util::value_to_stack(stack, std::apply([&fn](Args... args) -> Ret {
                return fn(std::move(args)...);
            }, util::values_from_stack<Args...>(stack)));
        }
    } catch (const util::ValueError &err) {
        stack.panic(err.what());
    }

    static MemoryManager::AutoPtr<VM::Stack>
    create_panicked_stack(VM &vm, const std::string &msg) {
        auto stack = vm.mem.gc_new_auto<VM::Stack>(vm);
        try {
            stack->panic(msg);
        } catch (...) {}
        return stack;
    }

    static MemoryManager::AutoPtr<VM::Stack>
    eval_fn(VM &vm, VM::Function *start) {
        auto stack = vm.mem.gc_new_auto<VM::Stack>(vm, start);
        stack->push_sep(); // No arguments
        stack->execute();
        return stack;
    }

    static MemoryManager::AutoPtr<VM::Stack>
    eval_expr(VM &vm, VM::Module *mod, VM::Scope *scope,
              const std::string &filename, const std::string &expr, const std::string &expr_name) try {
        // Split expression into array of tokens
        std::vector<Token> tokens;
        tokenize(filename, expr, [&tokens](auto token) { tokens.push_back(token); });
        // Parse array of tokens
        ast_ptr ast = parse(filename, tokens);
        // Compile the expression AST
        Assembler as;
        as.compile_expression(ast.get());
        // Assemble the whole expression bytecode
        std::string bytes(as.total_size(), '\0');
        as.assemble(bytes.data());
        auto bytecode = vm.mem.gc_new_auto<VM::Bytecode>(vm, bytes);
        // Create temporary environment for expression evaluation
        auto start = vm.mem.gc_new_auto<VM::BytecodeFunction>(vm, mod, scope, bytecode.get());
        start->assign_name(FStr(expr_name, vm.mem.str_alloc()));
        return eval_fn(vm, start.get());
    } catch (const CompilationError &err) {
        return create_panicked_stack(vm, std::string("compilation error: ") + err.what());
    } catch (const VM::StackOverflowError &) {
        return create_panicked_stack(vm, "stack overflow");
    } catch (const OutOfMemoryError &) {
        return create_panicked_stack(vm, "out of memory");
    }

    std::string display_value(const VM::Value &val) {
        std::ostringstream out;
        switch (val.type) {
            case Type::INT:
                out << val.data.num;
                break;
            case Type::FLP:
                out << val.data.flp;
                break;
            case Type::OBJ: {
                VM::Object &obj = *val.data.obj;
                if (obj.get_fields().empty()) {
                    out << '{';
                    for (size_t pos = 0; pos < obj.get_values().size(); pos++) {
                        if (pos) out << ", ";
                        out << display_value(obj.get_values()[pos]);
                    }
                    out << '}';
                } else out << "object(" << val.data.obj << ")";
                break;
            }
            case Type::FUN:
                out << val.data.fun->display();
                break;
            case Type::BLN:
                out << (val.data.bln ? "yes" : "no");
                break;
            case Type::STR:
                out << "'" << val.data.str->bytes << "'";
                break;
            case Type::ARR: {
                out << "[";
                VM::Array &arr = *val.data.arr;
                for (size_t pos = 0; pos < arr.len(); pos++) {
                    if (pos) out << ", ";
                    out << display_value(arr[pos]);
                }
                out << "]";
                break;
            }
            case Type::PTR: {
                out << "pointer(" << val.data.ptr << ")";
                break;
            }
            default:
                assertion_failed("unknown value");
        }
        return out.str();
    }

    static void print_panic(VM::Stack &stack) {
        if (!stack.is_panicked()) assertion_failed("no panic encountered");
        FVec<FStr> trace(stack.vm.mem.std_alloc<FStr>());
        stack.generate_stack_trace(std::back_inserter(trace));
        std::reverse(trace.begin(), trace.end());
        for (const auto &row : trace) {
            std::cerr << "! " << row << std::endl;
        }
        std::cerr << "! " << stack[-1].data.str->bytes << std::endl;
    }

    class ModuleLoadingError : public std::runtime_error {
    public:
        MemoryManager::AutoPtr<VM::Stack> stack;

        ModuleLoadingError(const std::string &mod_name, const std::string &why,
                           MemoryManager::AutoPtr<VM::Stack> &&stack) :
                std::runtime_error(mod_name + ": " + why), stack(std::move(stack)) {}

        ModuleLoadingError(const std::string &mod_name, const std::string &why) :
                ModuleLoadingError(mod_name, why, MemoryManager::AutoPtr<VM::Stack>(nullptr)) {}
    };

    static MemoryManager::AutoPtr<VM::Module>
    load_src_module(VM &vm, const std::string &name,
                    const std::vector<std::string> &imps, const std::vector<std::string> &deps) {
        // Read contents of the loader source file
        std::filesystem::path loader_path = get_src_module_loader_path(name);
        std::ifstream loader_file(loader_path);
        std::string loader_code;
        std::copy(std::istreambuf_iterator<char>(loader_file),
                  std::istreambuf_iterator<char>(),
                  std::back_inserter(loader_code));
        // Prepare module object and scope
        auto module_obj = vm.mem.gc_new_auto<VM::Object>(vm);
        module_obj->set_field(FStr(MODULE_EXPORTS_VAR, vm.mem.str_alloc()), Type::INT);
        module_obj->set_field(FStr(MODULE_RUNNER_VAR, vm.mem.str_alloc()), Type::INT);
        auto module_scope = vm.mem.gc_new_auto<VM::Scope>(module_obj.get(), nullptr);
        // Prepare module globals object and global scope
        auto module_globals = vm.mem.gc_new_auto<VM::Object>(vm);
        auto module_global_scope = vm.mem.gc_new_auto<VM::Scope>(module_globals.get(), module_scope.get());
        // Import any required modules into the module's global scope
        for (const auto &imp_mod : imps) {
            if (!vm.get_module(FStr(imp_mod, vm.mem.str_alloc())).has_value()) {
                throw ModuleLoadingError(name, "unable to import module " + imp_mod + " which is not registered");
            }
            auto exports_val = vm.get_module(FStr(imp_mod, vm.mem.str_alloc())).value()->object->
                    get_field(FStr(MODULE_EXPORTS_VAR, vm.mem.str_alloc())).value();
            if (exports_val.type != funscript::Type::OBJ) {
                throw ModuleLoadingError(name, "unable to import module " + imp_mod + " which has no exports");
            }
            for (const auto &[expt_name, expt_val] : exports_val.data.obj->get_fields()) {
                module_globals->set_field(expt_name, expt_val);
            }
        }
        auto mod = vm.mem.gc_new_auto<VM::Module>(vm, FStr(name, vm.mem.str_alloc()), module_globals.get(),
                                                  module_obj.get());
        // Register module dependencies
        for (const auto &dep_mod : deps) {
            if (!vm.get_module(FStr(dep_mod, vm.mem.str_alloc())).has_value()) {
                throw ModuleLoadingError(name, "invalid dependency " + dep_mod + ": the module is not registered");
            }
            mod->register_dependency(FStr(get_module_alias(dep_mod), vm.mem.str_alloc()),
                                     vm.get_module(FStr(dep_mod, vm.mem.str_alloc())).value());
        }
        // Execute module loader code
        auto stack = util::eval_expr(vm, mod.get(), module_global_scope.get(),
                                     loader_path.string(), loader_code, "'<load>'");
        if (stack->is_panicked()) {
            throw ModuleLoadingError(name, "module loader panicked", std::move(stack));
        }
        return mod;
    }

    static MemoryManager::AutoPtr<VM::Module>
    load_native_module(VM &vm, const std::string &name) {
        dlerror();
        auto lib = dlopen(get_native_module_lib_path(name).c_str(), RTLD_NOW);
        if (!lib) throw ModuleLoadingError(name, dlerror());
        auto module_exports = vm.mem.gc_new_auto<VM::Object>(vm);
        auto module_obj = vm.mem.gc_new_auto<VM::Object>(vm);
        auto mod = vm.mem.gc_new_auto<VM::Module>(vm, FStr(name, vm.mem.str_alloc()), nullptr, module_obj.get());
        auto *mod_ptr = mod.get();
        auto load_native_sym_fn = vm.mem.gc_new_auto<VM::NativeFunction>(
                vm, mod.get(), [mod_ptr, lib](VM::Stack &stack) -> void {
                    std::function load_native_sym([&stack, mod_ptr, lib](MemoryManager::AutoPtr<VM::String> sym) ->
                                                          MemoryManager::AutoPtr<VM::Function> {
                        dlerror();
                        auto *fn_ptr = reinterpret_cast<void (*)(VM::Stack &)>(dlsym(lib, sym->bytes.c_str()));
                        if (!fn_ptr) stack.panic(std::string("failed to load native symbol: ") + dlerror());
                        return stack.vm.mem.gc_new_auto<VM::NativeFunction>(stack.vm, mod_ptr, fn_ptr);
                    });
                    call_native_function(stack, load_native_sym);
                }
        );
        load_native_sym_fn->assign_name(FStr(NATIVE_MODULE_SYMBOL_LOADER_VAR, vm.mem.str_alloc()));
        module_exports->set_field(FStr(NATIVE_MODULE_SYMBOL_LOADER_VAR, vm.mem.str_alloc()),
                                  {Type::FUN, {.fun = load_native_sym_fn.get()}}
        );
        auto check_native_sym_fn = vm.mem.gc_new_auto<VM::NativeFunction>(
                vm, mod.get(), [lib](VM::Stack &stack) -> void {
                    std::function check_native_sym([lib](MemoryManager::AutoPtr<VM::String> sym) -> fbln {
                        dlerror();
                        auto *fn_ptr = reinterpret_cast<void (*)(VM::Stack &)>(dlsym(lib, sym->bytes.c_str()));
                        return fn_ptr != nullptr;
                    });
                    call_native_function(stack, check_native_sym);
                }
        );
        check_native_sym_fn->assign_name(FStr(NATIVE_MODULE_SYMBOL_CHECKER_VAR, vm.mem.str_alloc()));
        module_exports->set_field(FStr(NATIVE_MODULE_SYMBOL_CHECKER_VAR, vm.mem.str_alloc()),
                                  {Type::FUN, {.fun = check_native_sym_fn.get()}}
        );
        module_obj->set_field(FStr(MODULE_EXPORTS_VAR, vm.mem.str_alloc()),
                              {Type::OBJ, {.obj = module_exports.get()}});
        return mod;
    }

    static MemoryManager::AutoPtr<VM::Module>
    load_module(VM &vm, const std::string &name,
                const std::vector<std::string> &imps, const std::vector<std::string> &deps) {
        if (std::filesystem::exists(get_src_module_loader_path(name))) return load_src_module(vm, name, imps, deps);
        if (std::filesystem::exists(get_native_module_lib_path(name))) return load_native_module(vm, name);
        throw ModuleLoadingError(name, "failed to find module loader");
    }

}

#endif //FUNSCRIPT_UTILS_HPP
