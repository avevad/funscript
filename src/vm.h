//
// Created by avevad on 11/20/21.
//
#ifndef FUNSCRIPT_VM_H
#define FUNSCRIPT_VM_H

#include "common.h"

#include <map>
#include <string>
#include <vector>
#include <functional>

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

        using fun_def = std::function<void(Stack *, Frame *, const void *data, Scope *scope)>;

        struct Config {
            Allocator *alloc = nullptr;
        };

        const Config config;

        explicit VM(Config config);

        class Stack {
        public:
            VM &vm;

            Stack &operator=(const Stack &s) = delete;

            explicit Stack(VM &vm);

            [[nodiscard]] stack_pos_t size() const;
            const Value &operator[](stack_pos_t pos);
            [[nodiscard]] stack_pos_t abs(stack_pos_t pos) const;

            void push_sep();
            void push_nul();
            void push_int(int64_t num);
            void push_tab(Table *table);
            void push_ref(Scope *scope, const fstring &key);
            void push_val(Scope *scope, const fstring &key);
            void push_fun(fun_def def, const void *data, Scope *scope);

            void exec_bytecode(Frame *, const void *data, Scope *scope);
            void mov(bool discard = false);
            void dis();
            void op(Frame *frame, Operator op);

            Value pop();
            void pop(stack_pos_t pos);

            ~Stack();

        private:
            std::vector<Value, AllocatorWrapper<Value>> stack;

            void push(const Value &e);
            Value &get(stack_pos_t pos);
            stack_pos_t find_sep(stack_pos_t before = 0);
            void call(Function *fun, Frame *frame);
        };

        Stack &stack(size_t id);

        size_t new_stack();

        template<typename T>
        AllocatorWrapper<T> std_alloc() { return AllocatorWrapper<T>(config.alloc); }

        AllocatorWrapper<fchar> str_alloc() { return std_alloc<fchar>(); }

        template<typename T>
        T *allocate(size_t n = 1) { return reinterpret_cast<T *>(config.alloc->allocate(n * sizeof(T))); }

        void free(void *ptr) { config.alloc->free(ptr); }

    private:
        std::vector<Stack, AllocatorWrapper<Stack>> stacks;
    };

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
        explicit Frame(Frame *prev_frame) : prev_frame(prev_frame) {}
    };

    class Table {
    private:
        friend VM;

        fmap<fstring, Value> str_map;
        VM &vm;

    public:
        explicit Table(VM &vm) : vm(vm), str_map(vm.std_alloc<std::pair<const fstring, Value>>()) {};
        bool contains(const fstring &key);
        Value &var(const fstring &key);
    };

    class Scope {
    public:
        Table *const vars;
        Scope *const prev_scope;

        Scope(Table *vars, Scope *prev_scope) : vars(vars), prev_scope(prev_scope) {};

        [[nodiscard]] bool contains(const fstring &key) const;
        [[nodiscard]] Value &resolve(const fstring &key) const;
    };
}

#endif //FUNSCRIPT_VM_H
