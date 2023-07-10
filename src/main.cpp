#include <vector>
#include <sstream>

#include "tokenizer.hpp"
#include "vm.hpp"
#include "utils.hpp"

using namespace funscript;

struct module_conf_t {
    std::optional<std::string> name = std::nullopt; // The name of the module.
    std::vector<std::string> deps = {}; // The modules to be loaded before this module.
    std::vector<std::string> imps = {}; // The modules to be imported into this module's scope.
};

extern "C" const char *__asan_default_options() { // NOLINT(bugprone-reserved-identifier)
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
    if (!getenv(MODULES_PATH_ENV_VAR)) {
        std::cerr << args[0] << ": no modules path is set" << std::endl;
        return 1;
    }
    DefaultAllocator allocator(1073741824 /* 1 GiB */);
    VM vm({
                  .mm{.allocator = &allocator},
                  .stack_values_max = 67108864 /* 64 Mi */,
                  .stack_frames_max = 1024 /* 1 Ki */
          });
    for (const auto &module_conf : modules) {
        try {
            auto module_obj = util::load_module(vm, module_conf.name.value(), module_conf.imps, module_conf.deps);
            vm.register_module(FStr(module_conf.name.value(), vm.mem.str_alloc()), module_obj.get());
        } catch (const util::ModuleLoadingError &err) {
            std::cerr << args[0] << ": " << err.what() << std::endl;
            if (err.stack) util::print_panic(*err.stack);
            return 1;
        }
    }
    { // Start main module
        auto run_val = vm.get_module(FStr(modules.back().name.value(), vm.mem.str_alloc())).value()->
                object->get_field(FStr(MODULE_RUNNER_VAR, vm.mem.str_alloc())).value();
        if (run_val.type != Type::FUN) {
            std::cerr << args[0] << ": '" << modules.back().name.value() << "' is not runnable" << std::endl;
            return 1;
        }
        auto stack = util::eval_fn(vm, run_val.data.fun);
        if (stack->is_panicked()) {
            std::cerr << args[0] << ": main module panicked" << std::endl;
            util::print_panic(*stack);
            return 1;
        }
    }
}
