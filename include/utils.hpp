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
            static std::optional<fint> from_stack(VM &vm, VM::Stack &stack) {
                if (stack.top_value().type != Type::INT) return std::nullopt;
                fint result = stack.top_value().data.num;
                stack.pop_value();
                return result;
            }

            static void to_stack(VM &vm, VM::Stack &stack, fint num) {
                stack.push_int(num);
            }
        };

        template<>
        struct ValueTransformer<MemoryManager::AutoPtr<VM::Array>> {
            static std::optional<MemoryManager::AutoPtr<VM::Array>> from_stack(VM &vm, VM::Stack &stack) {
                if (stack.top_value().type != Type::ARR) return std::nullopt;
                auto result = MemoryManager::AutoPtr(stack.top_value().data.arr);
                stack.pop_value();
                return result;
            }

            static void to_stack(VM &vm, VM::Stack &stack, const MemoryManager::AutoPtr<VM::Array> &arr) {
                stack.push_arr(arr.get());
            }
        };

        template<>
        struct ValueTransformer<MemoryManager::AutoPtr<VM::String>> {
            static std::optional<MemoryManager::AutoPtr<VM::String>> from_stack(VM &vm, VM::Stack &stack) {
                if (stack.top_value().type != Type::STR) return std::nullopt;
                auto result = MemoryManager::AutoPtr(stack.top_value().data.str);
                stack.pop_value();
                return result;
            }

            static void to_stack(VM &vm, VM::Stack &stack, const MemoryManager::AutoPtr<VM::String> &str) {
                stack.push_str(str.get());
            }
        };

        template<>
        struct ValueTransformer<MemoryManager::AutoPtr<Allocation>> {
            static std::optional<MemoryManager::AutoPtr<Allocation>> from_stack(VM &vm, VM::Stack &stack) {
                if (stack.top_value().type != Type::PTR) return std::nullopt;
                auto result = MemoryManager::AutoPtr(stack.top_value().data.ptr);
                stack.pop_value();
                return result;
            }

            static void to_stack(VM &vm, VM::Stack &stack, const MemoryManager::AutoPtr<Allocation> &ptr) {
                stack.push_ptr(ptr.get());
            }
        };


        template<>
        struct ValueTransformer<MemoryManager::AutoPtr<VM::Function>> {
            static std::optional<MemoryManager::AutoPtr<VM::Function>> from_stack(VM &vm, VM::Stack &stack) {
                if (stack.top_value().type != Type::FUN) return std::nullopt;
                auto result = MemoryManager::AutoPtr(stack.top_value().data.fun);
                stack.pop_value();
                return result;
            }

            static void to_stack(VM &vm, VM::Stack &stack, const MemoryManager::AutoPtr<VM::Function> &ptr) {
                stack.push_fun(ptr.get());
            }
        };
    }

    template<typename T>
    static std::optional<T> value_from_stack(VM &vm, VM::Stack &stack) {
        return ValueTransformer<T>::from_stack(vm, stack);
    }

    template<typename T>
    static void value_to_stack(VM &vm, VM::Stack &stack, const T &val) {
        ValueTransformer<T>::to_stack(vm, stack, val);
    }

    class ValueError : public std::runtime_error {
    public:
        explicit ValueError(const std::string &what) : std::runtime_error(what) {}
    };

    namespace {

        std::tuple<> values_from_stack_impl(VM &vm, VM::Stack &stack, size_t pos) {
            if (stack.find_beg() != stack.size()) {
                throw ValueError("too many values, required " + std::to_string(pos));
            }
            stack.pop_pack();
            return {};
        }

        template<typename Values0>
        std::tuple<Values0> values_from_stack_impl(VM &vm, VM::Stack &stack, size_t pos) {
            if (stack.find_beg() == stack.size()) {
                throw ValueError("not enough values");
            }
            auto val = value_from_stack<Values0>(vm, stack);
            if (!val.has_value()) {
                throw ValueError("value #" + std::to_string(pos + 1) + " is of wrong type");
            }
            values_from_stack_impl(vm, stack, pos + 1);
            return {std::move(val.value())};
        }

        template<typename Values0, typename Values1, typename... Values>
        std::tuple<Values0, Values1, Values...>
        values_from_stack_impl(VM &vm, VM::Stack &stack, size_t pos) {
            if (stack.find_beg() == stack.size()) {
                throw ValueError("not enough values");
            }
            auto val = value_from_stack<Values0>(vm, stack);
            if (!val.has_value()) {
                throw ValueError("value #" + std::to_string(pos + 1) + " is of wrong type");
            }
            auto vals = values_from_stack_impl<Values1, Values...>(vm, stack, pos + 1);
            return std::tuple_cat(std::tuple<Values0>(std::move(val.value())), std::move(vals));
        }

    }

    template<typename... Values>
    std::tuple<Values...> values_from_stack(VM &vm, VM::Stack &stack) {
        if constexpr (sizeof...(Values) == 0) {
            return values_from_stack_impl(vm, stack, 0);
        } else {
            return values_from_stack_impl<Values...>(vm, stack, 0);
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
    call_native_function(VM &vm, VM::Stack &stack, const std::function<Ret(Args...)> &fn) {
        stack.reverse();
        try {
            auto args = util::values_from_stack<Args...>(vm, stack);
            auto ret = std::apply(fn, std::move(args));
            util::value_to_stack(vm, stack, ret);
        } catch (const util::ValueError &err) {
            stack.panic(err.what());
        }
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
        stack->separate(); // No arguments
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
            case Type::NUL:
                out << "nul";
                break;
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
        std::cerr << "! " << stack.top_value().data.str->bytes << std::endl;
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
        module_obj->set_field(FStr(MODULE_EXPORTS_VAR, vm.mem.str_alloc()), Type::NUL);
        module_obj->set_field(FStr(MODULE_STARTER_VAR, vm.mem.str_alloc()), Type::NUL);
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
        auto mod = vm.mem.gc_new_auto<VM::Module>(vm, module_globals.get(), module_obj.get());
        // Register module dependencies
        for (const auto &dep_mod : deps) {
            if (!vm.get_module(FStr(dep_mod, vm.mem.str_alloc())).has_value()) {
                throw ModuleLoadingError(name, "invalid dependency " + dep_mod + ": the module is not registered");
            }
            mod->register_dependency(FStr(dep_mod, vm.mem.str_alloc()),
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
        auto lib_handle = dlopen(get_native_module_lib_path(name).c_str(), RTLD_NOW);
        if (!lib_handle) {
            throw ModuleLoadingError(name, dlerror());
        }
        auto module_exports = vm.mem.gc_new_auto<VM::Object>(vm);
        auto module_obj = vm.mem.gc_new_auto<VM::Object>(vm);
        auto mod = vm.mem.gc_new_auto<VM::Module>(vm, nullptr, module_obj.get());
        VM::Module *mod_ptr = mod.get();
        auto native_sym_fn = vm.mem.gc_new_auto<VM::NativeFunction>(
                vm, mod.get(), [&vm, lib_handle, mod_ptr](VM::Stack &stack) -> void {
                    std::function fn([&stack, lib_handle, mod_ptr](
                            MemoryManager::AutoPtr<VM::String> name) -> MemoryManager::AutoPtr<VM::Function> {
                        auto *fn_ptr = reinterpret_cast<void (*)(VM::Stack &)>(
                                dlsym(lib_handle, name->bytes.c_str())
                        );
                        if (!fn_ptr) {
                            stack.panic(std::string("failed to load native symbol: ") + dlerror());
                        }
                        auto native_fn = stack.vm.mem.gc_new_auto<VM::NativeFunction>(
                                stack.vm, mod_ptr, [fn_ptr](VM::Stack &stack) -> void {
                                    return fn_ptr(stack);
                                }
                        );
                        return MemoryManager::AutoPtr(dynamic_cast<VM::Function *>(native_fn.get()));
                    });
                    call_native_function(vm, stack, fn);
                }
        );
        native_sym_fn->assign_name(FStr(NATIVE_MODULE_SYMBOL_LOADER_VAR, vm.mem.str_alloc()));
        module_exports->set_field(FStr(NATIVE_MODULE_SYMBOL_LOADER_VAR, vm.mem.str_alloc()),
                                  {Type::FUN, {.fun = native_sym_fn.get()}}
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
