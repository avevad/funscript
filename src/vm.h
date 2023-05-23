#ifndef FUNSCRIPT_VM_H
#define FUNSCRIPT_VM_H

#include <cstddef>
#include <cstdlib>
#include <functional>
#include <string>
#include <map>
#include <set>
#include <deque>
#include <optional>
#include <csignal>

#include "common.h"
#include "mm.h"

namespace funscript {

    class VM {
    public:
        VM(const VM &vm) = delete;
        VM &operator=(const VM &vm) = delete;

        struct Config {
            MemoryManager::Config mm;
            size_t stack_values_max = SIZE_MAX; // Maximum amount of stack values allowed in each execution stack.
            size_t stack_frames_max = SIZE_MAX; // Maximum amount of stack frames allowed in each execution stack.
        };

        class Stack;

        struct Value;

        /**
         * Class of array value objects.
         */
        class Array : public Allocation {
            VM &vm;
            fvec<Value> values;
            void get_refs(const std::function<void(Allocation *)> &callback) override;
        public:
            Array(VM &vm, Value *beg, size_t len);

            Array(VM &vm, size_t len);

            Array(VM &vm, const Array *arr1, const Array *arr2);

            const Value &operator[](size_t pos) const;

            void set(size_t pos, const Value &val);

            const Value *begin() const;

            const Value *end() const;

            [[nodiscard]] size_t len() const;

            ~Array();
        };

        /**
         * Class of string value objects.
         */
        class String : public Allocation {
            void get_refs(const std::function<void(Allocation *)> &callback) override;
        public:
            const fstr bytes;

            explicit String(fstr bytes);
        };

        /**
         * Class of error value objects.
         */
        class Error : public Allocation {
            void get_refs(const std::function<void(Allocation *)> &callback) override;
        public:
            const fstr desc; // Human-readable description of the error.

            explicit Error(fstr desc);
        };

        /**
         * Class of object value objects.
         */
        class Object : public Allocation {
        private:
            fumap<fstr, Value> fields; // Dictionary of object's fields.

            void get_refs(const std::function<void(Allocation *)> &callback) override;
        public:
            VM &vm;

            explicit Object(VM &vm) : vm(vm), fields(vm.mem.std_alloc<std::pair<const fstr, Value>>()) {};

            [[nodiscard]] bool contains_field(const fstr &key) const;
            [[nodiscard]] std::optional<Value> get_field(const fstr &key) const;
            void set_field(const fstr &key, Value val);

            ~Object() override;
        };

        /**
         * Class which represents the scope of an expression. Can be nested, in such case it contains the pointer to the parent scope.
         */
        class Scope : public Allocation {
            void get_refs(const std::function<void(Allocation *)> &callback) override;

            /**
             * Recursively searches the specified variable in the scope and all its parent scopes and updates the value of it.
             * @param name Name of the variable to search.
             * @param val The new value of the variable.
             * @param first The scope where the variable should be created if wasn't found.
             */
            void set_var(const fstr &name, Value val, Scope &first);
        public:
            Object *const vars; // Object which contains all the variables of the scope.
            Scope *const prev_scope; // Pointer to the parent scope.

            Scope(Object *vars, Scope *prev_scope);

            /**
             * Recursively searches the specified variable in the scope and all its parent scopes and retrieves the value of it.
             * @param name Name of the variable to search.
             * @return The value of the requested variable.
             */
            [[nodiscard]] std::optional<Value> get_var(const funscript::fstr &name) const;

            /**
             * Recursively searches the specified variable in the scope and all its parent scopes and updates the value of it.
             * @param name Name of the variable to search.
             * @param val The new value of the variable.
             */
            void set_var(const fstr &name, Value val);

            ~Scope();
        };

        class Function;

        /**
         * Objects of this class hold information about VM stack frame.
         */
        class Frame : public Allocation {
            Stack &stack;
            Function *cont_fn; // The function to be called in this frame.

            void get_refs(const std::function<void(Allocation *)> &callback) override;
        public:
            explicit Frame(Stack &stack, Function *cont_fn);

            void set_cont_fn(Function *cont_fn);

            Function *get_cont_fn() const;

            ~Frame();
        };

        /**
         * Class of function value objects.
         */
        class Function : public Allocation {
            friend VM::Stack;

            virtual void call(VM::Stack &stack, Frame *frame) = 0;
        public:
            Function() = default;
            ~Function() override = default;
        };

        class BytecodeFunction;

        /**
         * Class of bytecode holder objects.
         */
        class Bytecode : public Allocation {
            friend BytecodeFunction;
            friend VM::Stack;
            const std::string bytes;
        public:
            explicit Bytecode(std::string data);

            void get_refs(const std::function<void(Allocation *)> &callback) override;

            ~Bytecode() = default;
        };

        /**
         * Class of plain bytecode-compiled function value objects.
         */
        class BytecodeFunction : public Function {
            Scope *scope;
            Bytecode *bytecode;
            size_t offset;

            void call(VM::Stack &stack, Frame *frame) override;
        public:
            void get_refs(const std::function<void(Allocation *)> &callback) override;

            BytecodeFunction(Scope *scope, Bytecode *bytecode, size_t offset = 0);

            ~BytecodeFunction();
        };

        /**
         * Structure that holds Funscript value of any type.
         */
        struct Value {
            union Data {
                fint num;
                Object *obj;
                Function *fun;
                bool bln;
                String *str;
                Error *err;
                Array *arr;
            };
            Type type = Type::NUL;
            Data data = {.obj = nullptr};

            void get_ref(const std::function<void(Allocation *)> &callback) const {
                if (type == Type::OBJ) callback(data.obj);
                if (type == Type::FUN) callback(data.fun);
                if (type == Type::STR) callback(data.str);
                if (type == Type::ERR) callback(data.err);
                if (type == Type::ARR) callback(data.arr);
            }
        };

        const Config config; // Configuration of current VM instance.
        MemoryManager mem; // Memory manager for the current VM.

        explicit VM(Config config);

        /**
         * Class of Funscript execution stack.
         */
        class Stack : public Allocation {
            void get_refs(const std::function<void(Allocation *)> &callback) override;
        public:

            using pos_t = ssize_t; // Type representing position in stack. Can be negative (-1 is the topmost element).

            VM &vm; // The VM instance which holds the stack.

            Stack &operator=(const Stack &) = delete;
            Stack(const Stack &) = delete;

            /**
             * @param vm The VM instance which owns the stack.
             * @param start The main function of this routine.
             */
            explicit Stack(VM &vm, Function *start);

            [[nodiscard]] pos_t size() const;
            const Value &operator[](pos_t pos); // Value stack indexing.

            static volatile std::sig_atomic_t kbd_int; // This flag is used to interrupt running execution stack.

            void exec_bytecode(Scope *scope, Bytecode *bytecode_obj, size_t offset, pos_t frame_start);
            void call_operator(Operator op, Function *cont_fn);
            void call_assignment(Function *cont_fn);
            void call_function(Function *fun, Function *cont_fn);
            void continue_execution();

            // Some functions for pushing values onto the value stack.

            [[nodiscard]] bool push_sep();
            [[nodiscard]] bool push_nul();
            [[nodiscard]] bool push_int(fint num);
            [[nodiscard]] bool push_obj(Object *obj);
            [[nodiscard]] bool push_fun(Function *fun);
            [[nodiscard]] bool push_str(String *str);
            [[nodiscard]] bool push_bln(bool bln);
            [[nodiscard]] bool push_err(Error *err);
            [[nodiscard]] bool push_arr(Array *arr);

            void raise_err(const std::string &msg, pos_t frame_start);

            void raise_op_err(Operator op);

            /**
             * Weak conversion of value pack to boolean.
             * @return
             */
            void as_boolean();

            /**
             * Discards values until (and including) the topmost separator.
             */
            void discard();

            /**
             * Pops values until (and including) the value at position `pos`.
             * @param pos The position of bottommost element to pop.
             */
            void pop(pos_t pos = -1);

            /**
             * Finds the topmost separator value in value stack.
             * @param before Upper bound of search.
             * @return Absolute (non-negative) position of the requested separator.
             */
            pos_t find_sep(pos_t before = 0);

            ~Stack() override;

        private:
            fvec<Value> values; // Values stack.
            fvec<Frame *> frames; // Frames stack.

            /**
             * Pushes any value onto the value stack.
             * @param e Value to push.
             */
            [[nodiscard]] bool push(const Value &e);

            /**
             * Returns mutable reference to the value stack element at the specified position.
             * @param pos Position to index.
             * @return The value at the specified position of value stack.
             */
            const Value &get(pos_t pos);
        };
    };

    namespace {
        /**
         * Stub class used to implement routine yielding via stack unwinding.
         * The objects of this class are thrown in case of yield.
         */
        class __yield {
        };
    }
}

#endif //FUNSCRIPT_VM_H
