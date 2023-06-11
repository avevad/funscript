#include <vector>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <dlfcn.h>

#include "tokenizer.hpp"
#include "vm.hpp"
#include "utils.hpp"

using namespace funscript;

const char *MODULE_LOAD_FILENAME = "_load.fs";
const char *MODULE_EXPORTS_VAR = "exports";
const char *MODULE_START_VAR = "start";

static std::string get_module_alias(const std::string &name) {
    return name.substr(name.rfind('-') + 1);
}

static std::string get_module_base_path(const std::filesystem::path &modules_path, std::string name) {
    for (char &c : name) if (c == '.') c = std::filesystem::path::preferred_separator;
    return (modules_path / name).string();
}

static std::string get_module_native_lib_path(const std::filesystem::path &modules_path, const std::string &name) {
    return get_module_base_path(modules_path, name) + ".so";
}

static std::string get_module_loader_src_path(const std::filesystem::path &modules_path, const std::string &name) {
    return std::filesystem::path(get_module_base_path(modules_path, name)) / MODULE_LOAD_FILENAME;
}


struct module_conf_t {
    std::optional<std::string> name = std::nullopt; // The name of the module.
    std::vector<std::string> deps = {}; // The modules to be loaded before this module.
    std::vector<std::string> imps = {}; // The modules to be imported into this module's scope.
};

extern "C" const char *__asan_default_options() {
    return "detect_odr_violation=1";
}

int main(int argc, const char **argv) {
    std::vector<std::string> args(argv, argv + argc);
    std::vector<module_conf_t> modules;
    modules.emplace_back();
    for (size_t pos = 1; pos < argc;) {
        if (modules.back().name.has_value()) modules.emplace_back(); // If the module name was encountered, we should proceed to configuration of the next module
        const auto &arg = args[pos];
        if (arg == "-m") {
            pos++;
            if (pos >= argc) {
                std::cerr << args[0] << ": " << arg << ": module name expected" << std::endl;
                return 1;
            }
            modules.back().name = args[pos];
            pos++;
            continue;
        }
        if (arg == "-d") {
            pos++;
            if (pos >= argc) {
                std::cerr << args[0] << ": " << arg << ": module name expected" << std::endl;
                return 1;
            }
            modules.back().deps.push_back(args[pos]);
            pos++;
            continue;
        }
        if (arg == "-i") {
            if (pos >= argc) {
                std::cerr << args[0] << ": " << arg << ": module name expected" << std::endl;
                return 1;
            }
            modules.back().imps.push_back(args[++pos]);
            pos++;
            continue;
        }
        std::cerr << args[0] << ": " << arg << ": invalid option" << std::endl;
        return 1;
    }
    if (!modules.back().name.has_value()) {
        std::cerr << args[0] << ": module name expected" << std::endl;
        return 1;
    }
    std::string modules_path_str = getenv("FS_MODULES_PATH") ?: "";
    if (modules_path_str.empty()) {
        std::cerr << args[0] << ": no modules path is set" << std::endl;
        return 1;
    }
    std::filesystem::path modules_path(modules_path_str);
    DefaultAllocator allocator(1073741824 /* 1 GiB */);
    VM vm({
                  .mm{.allocator = &allocator},
                  .stack_values_max = 67108864 /* 64 Mi */,
                  .stack_frames_max = 1024 /* 1 Ki */
          });
    auto load_module_native = [&](const module_conf_t &module_conf) -> MemoryManager::AutoPtr<VM::Module> {
        dlerror();
        auto lib_handle = dlopen(get_module_native_lib_path(modules_path, module_conf.name.value()).c_str(), RTLD_NOW);
        if (!lib_handle) {
            std::cerr << args[0] << ": can't load native module '" << module_conf.name.value() << "': " << dlerror()
                      << std::endl;
            return {vm.mem, nullptr};
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
        module_exports->set_field(
                FStr("load_native_sym", vm.mem.str_alloc()),
                {.type = Type::FUN, .data = {.fun = native_sym_fn.get()}}
        );
        module_obj->set_field(FStr(MODULE_EXPORTS_VAR, vm.mem.str_alloc()),
                              {.type = Type::OBJ, .data = {.obj = module_exports.get()}});
        return mod;
    };
    auto load_module = [&](const module_conf_t &module_conf) -> MemoryManager::AutoPtr<VM::Module> {
        std::filesystem::path load_path = get_module_loader_src_path(modules_path, module_conf.name.value());
        if (!exists(load_path)) return load_module_native(module_conf);
        // Read contents of the loader source file
        std::ifstream load_file(load_path);
        std::string load_code;
        std::copy(std::istreambuf_iterator<char>(load_file),
                  std::istreambuf_iterator<char>(),
                  std::back_inserter(load_code));
        // Prepare module object and scope
        auto module_obj = vm.mem.gc_new_auto<VM::Object>(vm);
        module_obj->set_field(FStr(MODULE_EXPORTS_VAR, vm.mem.str_alloc()), {.type = Type::NUL});
        module_obj->set_field(FStr(MODULE_START_VAR, vm.mem.str_alloc()), {.type = Type::NUL});
        auto module_scope = vm.mem.gc_new_auto<VM::Scope>(module_obj.get(), nullptr);
        // Prepare module globals object and global scope
        auto module_globals = vm.mem.gc_new_auto<VM::Object>(vm);
        auto module_global_scope = vm.mem.gc_new_auto<VM::Scope>(module_globals.get(), module_scope.get());
        // Import any required modules into the module's global scope
        for (const auto &imp_mod : module_conf.imps) {
            if (!vm.get_module(FStr(imp_mod, vm.mem.str_alloc())).has_value()) {
                std::cerr << args[0] << ": '"
                          << imp_mod << "' is not loaded, but must be imported in '" << module_conf.name.value() << "'"
                          << std::endl;
                return {vm.mem, nullptr};
            }
            auto exports_val = vm.get_module(FStr(imp_mod, vm.mem.str_alloc())).value()->object->
                    get_field(FStr(MODULE_EXPORTS_VAR, vm.mem.str_alloc())).value();
            if (exports_val.type != funscript::Type::OBJ) {
                std::cerr << args[0] << ": '"
                          << imp_mod << "' has no exports, but must be imported in '" << module_conf.name.value() << "'"
                          << std::endl;
                return {vm.mem, nullptr};
            }
            for (const auto &[expt_name, expt_val] : exports_val.data.obj->get_fields()) {
                module_globals->set_field(expt_name, expt_val);
            }
        }
        auto mod = vm.mem.gc_new_auto<VM::Module>(vm, module_globals.get(), module_obj.get());
        // Register module dependencies
        for (const auto &dep_mod : module_conf.deps) {
            if (!vm.get_module(FStr(dep_mod, vm.mem.str_alloc())).has_value()) {
                std::cerr << args[0] << ": '"
                          << dep_mod << "' is not loaded, but is a dependency of '" << module_conf.name.value() << "'"
                          << std::endl;
                return {vm.mem, nullptr};
            }
            mod->register_dependency(FStr(dep_mod, vm.mem.str_alloc()),
                                     vm.get_module(FStr(dep_mod, vm.mem.str_alloc())).value());
        }
        // Execute module loader code
        auto stack = util::eval_expr(vm, mod.get(), module_global_scope.get(), load_path.string(), load_code);
        if (stack->size() != 0 && (*stack)[-1].type == Type::ERR) {
            assertion_failed("failed to load module");
        }
        return mod;
    };
    for (const auto &module_conf : modules) {
        auto module_obj = load_module(module_conf);
        if (!module_obj) return 1;
        vm.load_module(FStr(module_conf.name.value(), vm.mem.str_alloc()), module_obj.get());
    }
    { // Start main module
        auto start_val = vm.get_module(FStr(modules.back().name.value(), vm.mem.str_alloc())).value()->
                object->get_field(FStr(MODULE_START_VAR, vm.mem.str_alloc())).value();
        if (start_val.type != Type::FUN) {
            std::cerr << args[0] << ": '" << modules.back().name.value() << "' has no start function" << std::endl;
            return 1;
        }
        auto stack = util::eval_fn(vm, start_val.data.fun);
        if (stack->size() != 0 && (*stack)[-1].type == Type::ERR) {
            assertion_failed("failed to start module");
        }
    }
}
