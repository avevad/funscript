#include <vector>
#include <iostream>

#include "tokenizer.h"
#include "ast.h"
#include "vm.h"

void print_stack_values(funscript::VM::Stack &stack) {
    if (stack.size() > 0) {
        for (funscript::VM::Stack::pos_t pos = 0; pos < stack.size(); pos++) {
            funscript::Value val = stack[pos];
            switch (val.type) {
                case funscript::Type::NUL:
                    std::cout << "nul";
                    break;
                case funscript::Type::INT:
                    std::cout << val.data.num;
                    break;
                case funscript::Type::OBJ:
                    std::cout << "object(" << val.data.obj << ")";
                    break;
                case funscript::Type::FUN:
                    std::cout << "function(" << val.data.fun << ")";
                    break;
                case funscript::Type::BLN:
                    std::cout << (val.data.bln ? "yes" : "no");
                    break;
                default:
                    throw std::runtime_error("unknown value");
            }
            if (pos != stack.size() - 1) std::cout << ", ";
        }
    }
}

int main() {
    // Create a VM instance
    funscript::DefaultAllocator allocator;
    funscript::VM vm({.allocator = &allocator});
    // Create the global environment for the expression evaluation
    auto *globals = vm.mem.gc_new<funscript::Object>(vm);
    auto *scope = vm.mem.gc_new<funscript::Scope>(globals, nullptr);
    while (true) {
        // Read expression from stdin
        std::cout << ": ";
        std::string code;
        if (!std::getline(std::cin, code)) break;
        // Split expression into array of tokens
        std::vector<funscript::Token> tokens;
        funscript::tokenize(code, [&tokens](auto token) { tokens.push_back(token); });
        // Parse array of tokens
        funscript::ast_ptr ast = funscript::parse(tokens);
        // Compile the expression AST
        funscript::Assembler as;
        as.compile_expression(ast.get());
        // Assemble the whole expression bytecode
        std::string bytes(as.total_size(), '\0');
        as.assemble(bytes.data());
        auto *bytecode = vm.mem.gc_new<funscript::Bytecode>(bytes);
        // Create temporary environment for expression evaluation
        auto *start = vm.mem.gc_new<funscript::BytecodeFunction>(scope, bytecode);
        vm.mem.gc_unpin(bytecode);
        auto *stack = vm.mem.gc_new<funscript::VM::Stack>(vm, start);
        vm.mem.gc_unpin(start);
        // Evaluate the expression and print the result
        stack->continue_execution();
        if (stack->size() != 0) {
            std::cout << "= ";
            print_stack_values(*stack);
            std::cout << std::endl;
        }
        // Intermediate cleanup
        vm.mem.gc_unpin(stack);
        vm.mem.gc_cycle();
    }
    // Final cleanup
    vm.mem.gc_unpin(scope);
    vm.mem.gc_unpin(globals);
    return 0;
}
