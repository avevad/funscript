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
#include <set>

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

        VM(const VM &vm) = delete;
        VM &operator=(const VM &vm) = delete;

        using fun_def = std::function<void(Stack *, Frame *, const void *data, Scope *scope)>;

        struct Config {
            Allocator *alloc = nullptr;
        };

        class MemoryManager;

        class Allocation {
            friend MemoryManager;
        public:
            virtual ~Allocation() = default;
        };

        class MemoryManager {
        public:
            VM &vm;
            std::set<Allocation *, std::less<>, AllocatorWrapper<Allocation *>> gc_tracked;

            explicit MemoryManager(VM &vm) : vm(vm), gc_tracked(std_alloc<Allocation *>()) {}

            template<typename T>
            AllocatorWrapper<T> std_alloc() { return AllocatorWrapper<T>(vm.config.alloc); }

            AllocatorWrapper<fchar> str_alloc() { return std_alloc<fchar>(); }

            template<typename T>
            T *allocate() { return reinterpret_cast<T *>(vm.config.alloc->allocate(sizeof(T))); }

            void free(void *ptr) { vm.config.alloc->free(ptr); }

            void gc_track(Allocation *alloc);

            template<class T, typename... A>
            T *gc_new(A &&... args) {
                T *ptr = new(allocate<T>()) T(std::forward<A>(args)...);
                gc_track(ptr);
                return ptr;
            }

            ~MemoryManager();
        };

        const Config config;
        MemoryManager mem;

        explicit VM(Config config);

        friend MemoryManager;

        class Stack {
        public:
            VM &vm;

            Stack &operator=(const Stack &s) = delete;
            Stack(const Stack &s) = delete;

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

        ~VM();

    private:
        std::vector<Stack *, AllocatorWrapper<Stack *>> stacks;
    };

    struct Function : public VM::Allocation {
        VM::fun_def def;
        const void *data;
        Scope *scope;

        Function(VM::fun_def def, const void *data, Scope *scope) : def(def), data(data), scope(scope) {}
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

    class Frame : public VM::Allocation {
        Frame *prev_frame;
    public:
        explicit Frame(Frame *prev_frame) : prev_frame(prev_frame) {}

        ~Frame() = default;
    };

    class Table : public VM::Allocation {
    private:
        friend VM;

        fmap<fstring, Value> str_map;
        VM &vm;

    public:
        explicit Table(VM &vm) : vm(vm), str_map(vm.mem.std_alloc<std::pair<const fstring, Value>>()) {};
        bool contains(const fstring &key);
        Value &var(const fstring &key);
        ~Table() = default;
    };

    class Scope : public VM::Allocation {
    public:
        Table *const vars;
        Scope *const prev_scope;

        Scope(Table *vars, Scope *prev_scope) : vars(vars), prev_scope(prev_scope) {};

        [[nodiscard]] bool contains(const fstring &key) const;
        [[nodiscard]] Value &resolve(const fstring &key) const;
    };
}

#endif //FUNSCRIPT_VM_H
