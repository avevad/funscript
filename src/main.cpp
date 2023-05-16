#include <vector>
#include <iostream>
#include <sstream>

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

int main() {
    // Create a VM instance
    DefaultAllocator allocator;
    VM vm({.allocator = &allocator});
    // Create the global environment for the expression evaluation
    auto *globals = vm.mem.gc_new<VM::Object>(vm);
    auto *scope = vm.mem.gc_new<VM::Scope>(globals, nullptr);
    while (true) {
        // Read expression from stdin
        std::cout << ": ";
        std::string code;
        if (!std::getline(std::cin, code)) break;
        try {
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
            auto *bytecode = vm.mem.gc_new<VM::Bytecode>(bytes);
            // Create temporary environment for expression evaluation
            auto *start = vm.mem.gc_new<VM::BytecodeFunction>(scope, bytecode);
            vm.mem.gc_unpin(bytecode);
            auto *stack = vm.mem.gc_new<VM::Stack>(vm, start);
            vm.mem.gc_unpin(start);
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
            vm.mem.gc_unpin(stack);
            vm.mem.gc_cycle();
        } catch (const CodeReadingError &err) {
            std::cout << "! syntax error: " << err.what() << std::endl;
        } catch (const CompilationError &err) {
            std::cout << "! compilation error: " << err.what() << std::endl;
        }
    }
    // Final cleanup
    vm.mem.gc_unpin(scope);
    vm.mem.gc_unpin(globals);
    return 0;
}
