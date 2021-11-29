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

    class Scope;

    class Value;

    class Frame;

    class Function;

    typedef ssize_t stack_pos_t;

    class VM {
    public:
        class Stack;

        typedef void *(*heap_allocator)(void *, size_t);
        using fun_def = void (*)(Stack *, Frame *, const void *data, Scope *scope);

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
            [[nodiscard]] stack_pos_t abs(stack_pos_t pos) const;

            void push_sep();
            void push_nul();
            void push_int(int64_t num);
            void push_tab();
            void push_ref(Scope *scope, const std::wstring &key);
            void push_val(Scope *scope, const std::wstring &key);
            void push_fun(fun_def def, const void *data, Scope *scope);

            void mov(bool discard = false);
            void dis();
            void op(Frame *frame, Operator op);

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
            void call(Function *fun, Frame *frame);
        };

        Stack &stack(size_t id);

        size_t new_stack();
    private:
        std::vector<Stack> stacks;
    };

    void exec_bytecode(VM::Stack *stack, Frame *, const void *data, Scope *scope);

    struct Function {
        VM::fun_def def;
        const void *data;
        Scope *scope;
    };

    struct Value {
        enum Type {
            NUL, SEP, INT, TAB, REF, FUN
        };
        union Data {
            int64_t num;
            Table *tab;
            Value *ref;
            Function *fun;
        };
        Type type = NUL;
        Data data = {.tab = nullptr};
    };

    class Frame {
        Frame *prev_frame;
    public:
        Frame(Frame *prev_frame) : prev_frame(prev_frame) {}
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
    public:
        Scope *const prev_scope;

        Scope(Table *vars, Scope *prev_scope) : vars(vars), prev_scope(prev_scope) {};

        [[nodiscard]] bool contains(const std::wstring &key) const;
        Value &resolve(const std::wstring &key);
    };
}

#endif //FUNSCRIPT_VM_H
