#ifndef FUNSCRIPT_UTILS_HPP
#define FUNSCRIPT_UTILS_HPP

#include "tokenizer.hpp"
#include "ast.hpp"
#include "vm.hpp"

namespace funscript::util {

    MemoryManager::AutoPtr<VM::Stack>
    eval_fn(VM &vm, VM::Function *start) {
        auto stack = vm.mem.gc_new_auto<VM::Stack>(vm, start);
        stack->continue_execution();
        return stack;
    }

    MemoryManager::AutoPtr<VM::Stack>
    eval_expr(VM &vm, VM::Module *mod, VM::Scope *scope, const std::string &filename, const std::string &expr) try {
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
        auto bytecode = vm.mem.gc_new_auto<VM::Bytecode>(bytes);
        // Create temporary environment for expression evaluation
        auto start = vm.mem.gc_new_auto<VM::BytecodeFunction>(mod, scope, bytecode.get());
        start->assign_name(FStr("'<start>'", vm.mem.str_alloc()));
        return eval_fn(vm, start.get());
    } catch (const CodeReadingError &err) {
        auto stack = vm.mem.gc_new_auto<VM::Stack>(vm);
        stack->raise_err(std::string("syntax error: ") + err.what(), 0);
        return stack;
    } catch (const CompilationError &err) {
        auto stack = vm.mem.gc_new_auto<VM::Stack>(vm);
        stack->raise_err(std::string("compilation error: ") + err.what(), 0);
        return stack;
    } catch (const VM::StackOverflowError &) {
        auto stack = vm.mem.gc_new_auto<VM::Stack>(vm);
        stack->raise_err(std::string("stack overflow"), 0);
        return stack;
    } catch (const OutOfMemoryError &) {
        auto stack = vm.mem.gc_new_auto<VM::Stack>(vm);
        stack->raise_err(std::string("out of memory"), 0);
        return stack;
    }

    class ModuleLoadingError : public std::runtime_error {
    public:
        explicit ModuleLoadingError(const std::string &mod_name, const std::string &why) :
                std::runtime_error(mod_name + ": " + why) {}
    };

    MemoryManager::AutoPtr<VM::Module>
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
        module_obj->set_field(FStr(MODULE_EXPORTS_VAR, vm.mem.str_alloc()), {.type = Type::NUL});
        module_obj->set_field(FStr(MODULE_STARTER_VAR, vm.mem.str_alloc()), {.type = Type::NUL});
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
        auto stack = util::eval_expr(vm, mod.get(), module_global_scope.get(), loader_path.string(), loader_code);
        if (stack->size() != 0 && (*stack)[-1].type == Type::ERR) {
            assertion_failed("failed to load module");
        }
        return mod;
    }

    MemoryManager::AutoPtr<VM::Module>
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
                vm, mod.get(), [lib_handle, mod_ptr](VM::Stack &stack, VM::Frame *frame) -> void {
                    auto frame_start = stack.find_sep();
                    auto name_val = stack[-1];
                    if (name_val.type != Type::STR || stack[-2].type != Type::SEP) {
                        return stack.raise_err("symbol name is required", frame_start);
                    }
                    auto *fn_ptr = reinterpret_cast<void (*)(VM::Stack &, VM::Frame *)>(
                            dlsym(lib_handle, name_val.data.str->bytes.c_str())
                    );
                    if (!fn_ptr) {
                        return stack.raise_err(std::string("can't load native symbol: ") + dlerror(), frame_start);
                    }
                    stack.pop(frame_start);
                    auto native_fn = stack.vm.mem.gc_new_auto<VM::NativeFunction>(
                            stack.vm, mod_ptr, [fn_ptr](VM::Stack &stack, VM::Frame *frame) -> void {
                                return fn_ptr(stack, frame);
                            }
                    );
                    stack.push_fun(native_fn.get());
                }
        );
        module_exports->set_field(FStr(NATIVE_MODULE_SYMBOL_LOADER_VAR, vm.mem.str_alloc()),
                                  {.type = Type::FUN, .data = {.fun = native_sym_fn.get()}}
        );
        module_obj->set_field(FStr(MODULE_EXPORTS_VAR, vm.mem.str_alloc()),
                              {.type = Type::OBJ, .data = {.obj = module_exports.get()}});
        return mod;
    }

    MemoryManager::AutoPtr<VM::Module>
    load_module(VM &vm, const std::string &name,
                const std::vector<std::string> &imps, const std::vector<std::string> &deps) {
        if (std::filesystem::exists(get_src_module_loader_path(name))) return load_src_module(vm, name, imps, deps);
        if (std::filesystem::exists(get_native_module_lib_path(name))) return load_native_module(vm, name);
        throw ModuleLoadingError(name, "failed to find module loader");
    }
}

#endif //FUNSCRIPT_UTILS_HPP
