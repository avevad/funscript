#include "tokenizer.h"
#include "compiler.h"
#include "vm.h"

#include <cstddef>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <map>


int main() {
    // reading code from stdin
    std::wstring line, code;
    while (std::getline(std::wcin, line) && !line.empty()) code += line, code += L'\n';

    // tokenizing
    std::vector<funscript::Token> tokens;

    try {
        funscript::tokenize(code, [&tokens](const funscript::Token &token) { tokens.push_back(token); });
    } catch (const funscript::CompilationError &err) {
        std::cerr << "reading error: " << err.what() << std::endl;
        return 1;
    }

    // parsing
    funscript::AST *ast;
    try {
        ast = funscript::parse(tokens);
    } catch (const funscript::CompilationError &err) {
        std::cerr << "parsing error: " << err.what() << std::endl;
        return 1;
    }


    // compiling
    funscript::Assembler as;
    size_t cid = as.new_chunk();
    try {
        ast->compile_val(as, cid);
    } catch (const funscript::CompilationError &err) {
        std::cerr << "compilation error: " << err.what() << std::endl;
        return 1;
    }
    as.put_opcode(cid, funscript::Opcode::END);
    char *buf = reinterpret_cast<char *>(malloc(as.total_size()));
    as.assemble(buf);

    // executing
    funscript::VM vm({.stack_size = 4096});
    size_t sid = vm.new_stack();
    vm.stack(sid).push_tab();
    funscript::Table *globals = vm.stack(sid).pop().data.tab;
    auto *scope = new funscript::Scope(globals, nullptr);
    funscript::exec_bytecode(vm.stack(sid), buf, scope);

    // printing results
    for (funscript::stack_pos_t pos = 0; pos < vm.stack(sid).length(); pos++) {
        funscript::Value val = vm.stack(sid)[pos];
        switch (val.type) {
            case funscript::Value::NUL:
                std::wcout << L"nul";
                break;
            case funscript::Value::SEP:
                throw std::runtime_error(""); // TODO native error support
            case funscript::Value::INT:
                std::wcout << val.data.num;
                break;
            case funscript::Value::TAB:
                std::wcout << L"table(" << val.data.tab << ")";
                break;
            case funscript::Value::REF:
                throw std::runtime_error(""); // TODO native error support
        }
        if (pos != vm.stack(sid).length() - 1) std::wcout << L", ";
        else std::wcout << std::endl;
    }
    vm.stack(sid).pop(0);
}
