//
// Created by avevad on 11/20/21.
//
#ifndef FUNSCRIPT_VM_H
#define FUNSCRIPT_VM_H

#include "common.h"

#include <map>
#include <string>
#include <vector>

namespace funscript {

    class Table;

    class VM;

    struct Value {
        enum Type {
            NUL, SEP, INT, TAB, REF
        };
        union Data {
            int64_t num;
            Table *tab;
            Value *ref;
        };
        Type type = NUL;
        Data data = {.tab = nullptr};
    };

    class Table {
    private:
        friend VM;

        std::map<std::wstring, Value> str_map;
        VM &vm;

        explicit Table(VM &vm) : vm(vm) {};
    public:
        bool contains(const std::wstring &key);
        Value &var(const std::wstring &key);
    };

    class Scope {
    private:
        Table *const vars;
        Scope *const prev_scope;
    public:
        Scope(Table *vars, Scope *prev_scope) : vars(vars), prev_scope(prev_scope) {};

        Value &resolve(const std::wstring &key);
    };

    typedef ssize_t stack_pos_t;

    class VM {
    public:
        typedef void *(*heap_allocator)(void *, size_t);

        struct Config {
            heap_allocator alloc;
            size_t stack_size;
        };

        const Config config;

        explicit VM(Config config);

        class Stack {
        public:
            VM &vm;

            Stack &operator=(const Stack &s) = delete;

            explicit Stack(VM &vm);

            [[nodiscard]] stack_pos_t length() const;
            const Value &operator[](stack_pos_t pos);
            stack_pos_t abs(stack_pos_t pos) const;

            void push_sep();
            void push_nul();
            void push_int(int64_t num);
            void push_tab();
            void push_ref(Scope *scope, const std::wstring &key);
            void push_val(Scope *scope, const std::wstring &key);


            void add();
            void mov();
            void dis();
            void op(Operator op);

            Value pop();
            void pop(stack_pos_t pos);

            ~Stack();

        private:
            const size_t size;
            Value *const stack;
            stack_pos_t len = 0;

            void push(const Value &e);
            Value &get(stack_pos_t pos);
            stack_pos_t find_sep(stack_pos_t before = 0);
        };

        Stack &stack(size_t id);

        size_t new_stack();
    private:
        std::vector<Stack> stacks;
    };

    void exec_bytecode(VM::Stack &stack, const void *data, Scope *scope);
}

#endif //FUNSCRIPT_VM_H
