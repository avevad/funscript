#include "tokenizer.h"
#include "compiler.h"
#include "vm.h"

#include <cstddef>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <map>


int main() {
    std::locale::global(std::locale(""));

    std::cout << funscript::VERSION << std::endl;

    funscript::VM vm({});
    const size_t sid = vm.new_stack();
    vm.stack(sid).push_tab();
    funscript::Table *globals = vm.stack(sid).pop().data.tab;
    auto *scope = new funscript::Scope(globals, nullptr);

    while (true) {
        std::wstring code;
        std::cout << ": ";
        if (!std::getline(std::wcin, code)) break;

        std::vector<funscript::Token> tokens;
        try {
            funscript::tokenize(code, [&tokens](const funscript::Token &token) { tokens.push_back(token); });
        } catch (const funscript::CompilationError &err) {
            std::cerr << "reading error: " << err.what() << std::endl;
            continue;
        }

        funscript::AST *ast;
        try {
            ast = funscript::parse(tokens);
        } catch (const funscript::CompilationError &err) {
            std::cerr << "parsing error: " << err.what() << std::endl;
            continue;
        }

        funscript::Assembler as;
        try {
            as.compile_expression(ast);
        } catch (const funscript::CompilationError &err) {
            std::cerr << "compilation error: " << err.what() << std::endl;
            continue;
        }
        char *buf = reinterpret_cast<char *>(malloc(as.total_size()));
        as.assemble(buf);

        vm.stack(sid).exec_bytecode(nullptr, buf, scope);
        if (vm.stack(sid).size() == 0) continue;
        std::wcout << L"= ";
        for (funscript::stack_pos_t pos = 0; pos < vm.stack(sid).size(); pos++) {
            funscript::Value val = vm.stack(sid)[pos];
            switch (val.type) {
                case funscript::Value::NUL:
                    std::wcout << L"nul";
                    break;
                case funscript::Value::INT:
                    std::wcout << val.data.num;
                    break;
                case funscript::Value::TAB:
                    std::wcout << L"table(" << val.data.tab << ")";
                    break;
                case funscript::Value::FUN:
                    std::wcout << L"function(" << val.data.fun << ")";
                    break;
                default:
                    throw std::runtime_error("unknown value");
            }
            if (pos != vm.stack(sid).size() - 1) std::wcout << L", ";
            else std::wcout << std::endl;
        }
        vm.stack(sid).pop(0);
    }
}
