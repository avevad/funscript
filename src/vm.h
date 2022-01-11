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
#include <queue>
#include <iostream>

namespace funscript {

    class Object;

    class Scope;

    class Value;

    class Frame;

    class Function;

    class Bytecode;

    typedef ssize_t stack_pos_t;

    class VM {
    public:
        class Stack;

        VM(const VM &vm) = delete;
        VM &operator=(const VM &vm) = delete;

        struct Config {
            Allocator *allocator = nullptr;
        };

        class MemoryManager;

        class Allocation {
            friend MemoryManager;
            virtual void get_refs(const std::function<void(Allocation *)> &callback) = 0;
        public:
            virtual ~Allocation() = default;
        };

        class MemoryManager {
        public:
            VM &vm;
        private:
            fset<Allocation *> gc_tracked;
            fmap<Allocation *, size_t> gc_pins;
        public:

            explicit MemoryManager(VM &vm) : vm(vm), gc_tracked(std_alloc<Allocation *>()),
                                             gc_pins(std_alloc<std::pair<Allocation *const, size_t>>()) {}

            template<typename T>
            AllocatorWrapper<T> std_alloc() { return AllocatorWrapper<T>(vm.config.allocator); }

            AllocatorWrapper<fchar> str_alloc() { return std_alloc<fchar>(); }

            template<typename T>
            T *allocate(size_t n = 1) { return reinterpret_cast<T *>(vm.config.allocator->allocate(n * sizeof(T))); }

            void free(void *ptr) { vm.config.allocator->free(ptr); }

            void gc_track(Allocation *alloc);

            void gc_pin(Allocation *alloc);

            void gc_unpin(Allocation *alloc);

            template<class T, typename... A>
            T *gc_new(A &&... args) {
                T *ptr = new(allocate<T>()) T(std::forward<A>(args)...);
                gc_track(ptr);
                return ptr;
            }

            void gc_cycle();

            ~MemoryManager();
        };

        const Config config;
        MemoryManager mem;

        explicit VM(Config config);

        friend MemoryManager;

        class Stack : public Allocation{
            void get_refs(const std::function<void (Allocation *)> &callback) override;
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
            void push_obj(Object *obj);
            void push_fun(Function *fun);
            void push_bln(bool bln);

            bool as_boolean();

            void exec_bytecode(Frame *, Scope *scope, Bytecode *bytecode_obj, size_t offset = 0);
            void discard();
            void call_operator(Frame *frame, Operator op);

            void pop(stack_pos_t pos = -1);

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

    struct Value {
        enum Type {
            NUL, SEP, INT, OBJ, FUN, BLN
        };
        union Data {
            int64_t num;
            Object *obj;
            Function *fun;
            bool bln;
        };
        Type type = NUL;
        Data data = {.obj = nullptr};
    };

    class Object : public VM::Allocation {
    private:
        friend VM;

        fmap<fstring, Value> str_map;

        void get_refs(const std::function<void(Allocation * )> &callback) override;
    public:
        VM &vm;

    public:
        explicit Object(VM &vm) : vm(vm), str_map(vm.mem.std_alloc<std::pair<const fstring, Value>>()) {};
        [[nodiscard]] bool contains(const fstring &key) const;
        [[nodiscard]] Value get_val(const fstring &key) const;
        void set_val(const fstring &key, Value val);
        ~Object() override = default;
        bool equals(Object &obj) const;
    };

    class Scope : public VM::Allocation {
        void get_refs(const std::function<void(Allocation * )> &callback) override;

        void set_var(const fstring &name, Value val, Scope &first);

    public:
        Object *const vars;
        Scope *const prev_scope;

        Scope(Object *vars, Scope *prev_scope) : vars(vars), prev_scope(prev_scope) {};

        [[nodiscard]] Value get_var(const fstring &name) const;

        void set_var(const fstring &name, Value val);
    };

    class Function : public VM::Allocation {
    public:
        VM &vm;

        explicit Function(VM &vm) : vm(vm) {}

        virtual void operator()(VM::Stack &stack, Frame *frame) = 0;
    };

    class Bytecode : public VM::Allocation {
        char *const data;
        Allocator *const allocator;

        void get_refs(const std::function<void(Allocation * )> &callback) override {}

    public:
        Bytecode(char *data, Allocator *allocator) : data(data), allocator(allocator) {}

        const char *get_data();
        ~Bytecode();
    };

    class CompiledFunction : public Function {
        Bytecode *bytecode;
        size_t offset;
        Scope *scope;

        void get_refs(const std::function<void(Allocation * )> &callback) override;
    public:
        CompiledFunction(Scope *scope, Bytecode *bytecode, size_t offset) : Function(scope->vars->vm),
                                                                            bytecode(bytecode), scope(scope),
                                                                            offset(offset) {}

        void operator()(VM::Stack &stack, Frame *frame) override {
            stack.exec_bytecode(frame, scope, bytecode, offset);
        }
    };

    class Frame : public VM::Allocation {
        Frame *prev_frame;

        void get_refs(const std::function<void(Allocation * )> &callback) override;
    public:
        explicit Frame(Frame *prev_frame) : prev_frame(prev_frame) {}

        ~Frame() override = default;
    };
}

#endif //FUNSCRIPT_VM_H
