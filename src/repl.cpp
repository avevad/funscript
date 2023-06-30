#include "vm.hpp"
#include "utils.hpp"

#include <fstream>

using namespace funscript;

void sigint_handler(int) {
    VM::Stack::kbd_int = 1;
}

void run_code(VM &vm, VM::Scope *scope, const std::string &filename, const std::string &code) {
    // Register Ctrl+C handler
    struct sigaction act{}, act_old{};
    act.sa_handler = sigint_handler;
    sigaction(SIGINT, &act, &act_old);
    // Evaluate the expression and display its result
    auto stack = util::eval_expr(vm, nullptr, scope, filename, code, "'<test>'");
    if (stack->size() != 0) {
        if (stack->is_panicked()) {
            util::print_panic(*stack);
        } else {
            std::cout << "= ";
            for (VM::Stack::pos_t pos = 0; pos < stack->size(); pos++) {
                if (pos != 0) std::cout << ", ";
                std::cout << util::display_value(stack->raw_values()[pos]);
            }
            std::cout << std::endl;
        }
    }
    // Clean up & unregister handler
    vm.mem.gc_cycle();
    sigaction(SIGINT, &act_old, nullptr);
}

int main(int argc, char **argv) {
    if (argc != 1 && argc != 2) {
        std::cerr << "bad usage: invalid number of arguments" << std::endl;
        return 1;
    }
    // Create a VM instance
    DefaultAllocator allocator(1073741824 /* 1 GiB */);
    VM vm({
                  .mm{.allocator = &allocator},
                  .stack_values_max = 67108864 /* 64 Mi */,
                  .stack_frames_max = 1024 /* 1 Ki */
          });
    {
        // Create the global environment for the expression evaluation
        auto globals = vm.mem.gc_new_auto<VM::Object>(vm);
        auto scope = vm.mem.gc_new_auto<VM::Scope>(globals.get(), nullptr);
        if (argc == 2) { // Read code from specified file
            std::ifstream file(argv[1]);
            std::string code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            run_code(vm, scope.get(), std::string(argv[1]), code);
        } else {
            while (true) {
                std::cout << ": ";
                std::string code;
                if (!std::getline(std::cin, code)) break;
                if (code == "# exit") break;
                run_code(vm, scope.get(), "<stdin>", code);
            }
        }
    }
    return 0;
}