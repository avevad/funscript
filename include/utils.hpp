#ifndef FUNSCRIPT_UTILS_HPP
#define FUNSCRIPT_UTILS_HPP

#include "tokenizer.hpp"
#include "ast.hpp"
#include "vm.hpp"

namespace funscript::util {
    MemoryManager::AutoPtr<VM::Stack> eval_expr(VM &vm, VM::Scope *scope, const std::string &expr) try {
        // Split expression into array of tokens
        std::vector<Token> tokens;
        tokenize(expr, [&tokens](auto token) { tokens.push_back(token); });
        // Parse array of tokens
        ast_ptr ast = parse(tokens);
        // Compile the expression AST
        Assembler as;
        as.compile_expression(ast.get());
        // Assemble the whole expression bytecode
        std::string bytes(as.total_size(), '\0');
        as.assemble(bytes.data());
        auto bytecode = vm.mem.gc_new_auto<VM::Bytecode>(bytes);
        // Create temporary environment for expression evaluation
        auto start = vm.mem.gc_new_auto<VM::BytecodeFunction>(scope, bytecode.get());
        auto stack = vm.mem.gc_new_auto<VM::Stack>(vm, start.get());
        // Execute expression evaluation
        stack->continue_execution();
        return stack;
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
}

#endif //FUNSCRIPT_UTILS_HPP
