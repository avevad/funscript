#include "tokenizer.h"
#include "compiler.h"
#include "vm.h"

#include <cstddef>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <map>
#include <fstream>

template<typename T>
std::wstring addr_to_str(const T *ptr) {
    std::wstringstream str;
    str.imbue(std::locale("C"));
    str << ((const void *) ptr);
    return str.str();
}

std::wstring to_string(const funscript::Value &val) {
    switch (val.type) {
        case funscript::Value::NUL:
            return L"nul";
        case funscript::Value::INT:
            return std::to_wstring(val.data.num);
        case funscript::Value::OBJ:
            return L"object(" + addr_to_str(val.data.obj) + L")";
        case funscript::Value::FUN:
            return L"function(" + addr_to_str(val.data.fun) + L")";
        case funscript::Value::BLN:
            return val.data.bln ? L"yes" : L"no";
        case funscript::Value::ARR: {
            std::wstring result = L"[";
            for (size_t pos = 0; pos < val.data.arr->len; pos++) {
                result += to_string(val.data.arr->data[pos]);
                if (pos + 1 != val.data.arr->len) result += L", ";
            }
            result += L"]";
            return result;
        }
        case funscript::Value::STR: {
            return L"\"" + std::wstring(val.data.str->data, val.data.str->len) + L"\"";
        }
        default:
            throw std::runtime_error("unknown value");
    }
}

void print_stack_values(funscript::VM::Stack &stack, bool silent) {
    if (stack.size() > 0) {
        if (!silent) std::wcout << L"= ";
        for (funscript::stack_pos_t pos = 0; pos < stack.size(); pos++) {
            funscript::Value val = stack[pos];
            std::wcout << to_string(val);
            if (pos != stack.size() - 1) std::wcout << L", ";
            else std::wcout << std::endl;
        }
    }
}

void execute_code(funscript::VM::Stack &stack, funscript::Scope *scope, const std::wstring &code,
                  funscript::Allocator *allocator = new funscript::DefaultAllocator, bool silent = true) {
    funscript::VM &vm = stack.vm;

    std::vector<funscript::Token> tokens;
    try {
        funscript::tokenize(code, [&tokens](const funscript::Token &token) { tokens.push_back(token); });
    } catch (const funscript::CompilationError &err) {
        std::cerr << "reading error: " << err.what() << std::endl;
        return;
    }

    funscript::ast_ptr ast;
    try {
        ast = funscript::parse(tokens);
    } catch (const funscript::CompilationError &err) {
        std::cerr << "parsing error: " << err.what() << std::endl;
        return;
    }

    funscript::Assembler as;
    try {
        as.compile_expression(ast.get());
    } catch (const funscript::CompilationError &err) {
        std::cerr << "compilation error: " << err.what() << std::endl;
        return;
    }
    char *bytecode = vm.mem.allocate<char>(as.total_size());
    as.assemble(bytecode);

    auto *bytecode_obj = vm.mem.gc_new<funscript::Bytecode>(bytecode, allocator);
    stack.exec_bytecode(nullptr, scope, bytecode_obj, reinterpret_cast<size_t *>(bytecode)[0]);

    print_stack_values(stack, silent);
    stack.pop(0);
    vm.mem.gc_unpin(bytecode_obj);
    vm.mem.gc_cycle();
}

int main(int argc, char **argv) {
    if (argc != 1 && argc != 2) {
        std::cerr << "invalid number of arguments" << std::endl;
        return 1;
    }
    bool read_from_file = argc == 2;
    std::locale::global(std::locale(""));
    if (!read_from_file) std::cout << funscript::VERSION << std::endl;
    auto *allocator = new funscript::DefaultAllocator();
    {
        funscript::VM vm({.allocator = allocator});
        const size_t sid = vm.new_stack();
        auto *globals = vm.mem.gc_new<funscript::Object>(vm);
        auto *scope = vm.mem.gc_new<funscript::Scope>(globals, nullptr);
        vm.mem.gc_unpin(globals);

        if (read_from_file) {
            std::wifstream file(argv[1]);
            std::wstring code((std::istreambuf_iterator<wchar_t>(file)), std::istreambuf_iterator<wchar_t>());

            execute_code(vm.stack(sid), scope, code, allocator);
        } else {
            while (true) {
                std::wstring code;
                std::cout << ": ";
                if (!std::getline(std::wcin, code)) break;

                execute_code(vm.stack(sid), scope, code, allocator, false);
            }
        }
        vm.mem.gc_unpin(scope);
    }
    delete allocator;
}
