#include <vector>
#include <iostream>
#include <sstream>
#include <fstream>

#include "tokenizer.hpp"
#include "vm.hpp"
#include "utils.hpp"

using namespace funscript;

std::string display(const VM::Value &val) {
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
        case Type::OBJ:
            out << "object(" << val.data.obj << ")";
            break;
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
                out << display(arr[pos]);
            }
            out << "]";
            break;
        }
        default:
            assertion_failed("unknown value");
    }
    return out.str();
}

void sigint_handler(int) {
    VM::Stack::kbd_int = 1;
}

void run_code(VM &vm, VM::Scope *scope, const std::string &filename, const std::string &code) {
    // Register Ctrl+C handler
    struct sigaction act{}, act_old{};
    act.sa_handler = sigint_handler;
    sigaction(SIGINT, &act, &act_old);
    // Evaluate the expression and display its result
    auto stack = util::eval_expr(vm, scope, filename, code);
    if (stack->size() != 0) {
        if ((*stack)[-1].type == Type::ERR) {
            for (const auto &e : (*stack)[-1].data.err->stacktrace) {
                std::cout << "!";
                std::cout << " in " << e.function;
                std::cout << " at " << e.meta;
                std::cout << std::endl;
            }
            std::cout << "! ";
            auto err_val = (*stack)[-1].data.err->obj->get_field(FStr("msg", vm.mem.str_alloc()));
            if (err_val.has_value() && err_val.value().type == Type::STR) {
                std::cout << std::string(err_val.value().data.str->bytes);
            }
            std::cout << "\n";
        } else {
            std::cout << "= ";
            for (VM::Stack::pos_t pos = 0; pos < stack->size(); pos++) {
                if (pos != 0) std::cout << ", ";
                std::cout << display((*stack)[pos]);
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
                run_code(vm, scope.get(), "<stdin>", code);
            }
        }
    }
    return 0;
}
