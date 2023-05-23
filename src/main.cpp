#include <vector>
#include <iostream>
#include <sstream>
#include <fstream>

#include "tokenizer.h"
#include "ast.h"
#include "vm.h"

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
        case Type::OBJ:
            out << "object(" << val.data.obj << ")";
            break;
        case Type::FUN:
            out << "function(" << val.data.fun << ")";
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
            throw std::runtime_error("unknown value");
    }
    return out.str();
}

void sigint_handler(int) {
    VM::Stack::kbd_int = 1;
}

void run_code(VM &vm, VM::Scope *scope, const std::string &code) {
    struct sigaction act{}, act_old{};
    act.sa_handler = sigint_handler;
    sigaction(SIGINT, &act, &act_old);
    try {
        {
            // Split expression into array of tokens
            std::vector<Token> tokens;
            tokenize(code, [&tokens](auto token) { tokens.push_back(token); });
            // Parse array of tokens
            ast_ptr ast = parse(tokens);
            // Compile the expression AST
            Assembler as;
            as.compile_expression(ast.get());
            // Assemble the whole expression bytecode
            std::string bytes(as.total_size(), '\0');
            as.assemble(bytes.data());
            auto bytecode = vm.mem.gc_new_auto<VM::Bytecode>(bytes);
            if (!bytecode) {
                std::cout << "! out of memory" << std::endl;
                goto end;
            }
            // Create temporary environment for expression evaluation
            auto start = vm.mem.gc_new_auto<VM::BytecodeFunction>(scope, bytecode.get());
            if (!start) {
                std::cout << "! out of memory" << std::endl;
                goto end;
            }
            auto stack = vm.mem.gc_new_auto<VM::Stack>(vm, start.get());
            if (!stack) {
                std::cout << "! out of memory" << std::endl;
                goto end;
            }
            // Evaluate the expression and print the result
            stack->continue_execution();
            if (stack->size() != 0) {
                if ((*stack)[-1].type == Type::ERR) {
                    std::cout << "! " << (*stack)[-1].data.err->desc << std::endl;
                } else {
                    std::cout << "= ";
                    for (VM::Stack::pos_t pos = 0; pos < stack->size(); pos++) {
                        if (pos != 0) std::cout << ", ";
                        std::cout << display((*stack)[pos]);
                    }
                    std::cout << std::endl;
                }
            }
            // Intermediate cleanup
        }
        vm.mem.gc_cycle();
    } catch (const CodeReadingError &err) {
        std::cout << "! syntax error: " << err.what() << std::endl;
    } catch (const CompilationError &err) {
        std::cout << "! compilation error: " << err.what() << std::endl;
    }
    end:;
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
            run_code(vm, scope.get(), code);
        } else {
            while (true) {
                std::cout << ": ";
                std::string code;
                if (!std::getline(std::cin, code)) break;
                run_code(vm, scope.get(), code);
            }
        }
    }
    return 0;
}
