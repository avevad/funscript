#ifndef FUNSCRIPT_VM_HPP
#define FUNSCRIPT_VM_HPP

#include "common.hpp"
#include "mm.hpp"

#include <cstddef>
#include <cstdlib>
#include <functional>
#include <string>
#include <map>
#include <set>
#include <deque>
#include <optional>
#include <csignal>

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
            FVec<Value> values;
            void get_refs(const std::function<void(Allocation *)> &callback) override;
        public:
            Array(VM &vm, Value *beg, size_t len);

            Array(VM &vm, size_t len);

            Value &operator[](size_t pos);

            const Value &operator[](size_t pos) const;

            Value *begin();

            const Value *begin() const;

            Value *end();

            const Value *end() const;

            [[nodiscard]] size_t len() const;
        };

        /**
         * Class of string value objects.
         */
        class String final : public Allocation {
            void get_refs(const std::function<void(Allocation *)> &callback) override;
        public:
            const FStr bytes;

            explicit String(VM &vm, FStr bytes);
        };

        /**
         * Class of object value objects.
         */
        class Object : public Allocation {
        private:
            FVec<Value> values; // Dictionary of object's indexed values.
            FMap<FStr, Value> fields; // Dictionary of object's fields.

            void get_refs(const std::function<void(Allocation *)> &callback) override;
        public:
            explicit Object(VM &vm);

            [[nodiscard]] bool contains_field(const FStr &key) const;
            [[nodiscard]] bool contains_field(const char *key) const;
            [[nodiscard]] std::optional<Value> get_field(const FStr &key) const;
            [[nodiscard]] std::optional<Value> get_field(const char *key) const;
            void set_field(const FStr &key, Value val);
            const decltype(fields) &get_fields() const;

            void init_values(const Value *beg, const Value *end);
            const decltype(values) &get_values() const;

            ~Object() override = default;
        };

        /**
         * Class which represents the scope of an expression. Can be nested, in such case it contains the pointer to the parent scope.
         */
        class Scope final : public Allocation {
            void get_refs(const std::function<void(Allocation *)> &callback) override;

        public:
            Object *const vars; // Object which contains all the variables of the scope.
            Scope *const prev_scope; // Pointer to the parent scope.

            Scope(Object *vars, Scope *prev_scope);

            /**
             * Recursively searches the specified variable in the scope and all its parent scopes and retrieves the value of it.
             * @param name Name of the variable to search.
             * @return The value of the requested variable.
             */
            [[nodiscard]] std::optional<Value> get_var(const funscript::FStr &name) const;

            /**
             * Recursively searches the specified variable in the scope and all its parent scopes and updates the value of it.
             * @param name Name of the variable to search.
             * @param val The new value of the variable.
             * @return Whether the variable exists or not (if not, it won't be created).
             */
            bool set_var(const FStr &name, Value val);
        };

        /**
         * Structure that represents the execution metadata (filename, line, column)
         */
        struct code_met_t {
            const char *filename;
            code_pos_t position;
            Scope *scope;
        };

        class Function;

        /**
         * Objects of this class hold information about VM stack frame.
         */
        class Frame final : public Allocation {
            friend VM::Stack;
            code_met_t fallback_meta{.filename = nullptr, .position = {.row = 0, .col = 0}, .scope = nullptr};
            code_met_t *meta_ptr = &fallback_meta;

            void get_refs(const std::function<void(Allocation *)> &callback) override;
        public:
            Frame *const prev_frame;
            const size_t depth;
            Function *const fun; // The function to be called in this frame.

            explicit Frame(Function *fun, Frame *prev_frame = nullptr);

            const code_met_t &get_meta() const;
        };

        /**
         * Objects of this class hold information about loaded Funscript module.
         */
        class Module final : public Allocation {
            FMap<FStr, Module *> deps; // Registered dependencies of the module.

            void get_refs(const std::function<void(Allocation *)> &callback) override;

        public:
            Object *const globals; // Globals of the module.
            Object *const object; // Module object.
            const FStr name;

            Module(VM &vm, const FStr &name, Object *globals, Object *object);

            void register_dependency(const FStr &alias, Module *mod);

            std::optional<Module *> get_dependency(const FStr &alias);
        };

        /**
         * Class of function value objects.
         */
        class Function : public Allocation {
            friend VM::Stack;
            std::optional<FStr> name;

            virtual void call(VM::Stack &stack) = 0;
        public:
            Module *const mod; // The origin of the function.

            Function(VM &vm, Module *mod);
            ~Function() override = default;

            /**
             * Produces the string representation of the function.
             * @return A short string string that represents this function.
             */
            [[nodiscard]] virtual FStr display() const = 0;

            void get_refs(const std::function<void(Allocation *)> &callback) override;

            [[nodiscard]] const std::optional<FStr> &get_name() const;

            void assign_name(const FStr &as_name);
        };

        class BytecodeFunction;

        /**
         * Class of bytecode holder objects.
         */
        class Bytecode final : public Allocation {
            friend BytecodeFunction;
            friend VM::Stack;
            const std::string bytes;
        public:
            explicit Bytecode(VM &vm, std::string data);

            void get_refs(const std::function<void(Allocation *)> &callback) override;

            ~Bytecode() override = default;
        };

        /**
         * Class of plain bytecode-compiled function value objects.
         */
        class BytecodeFunction final : public Function {
            Scope *scope;
            Bytecode *bytecode;
            size_t offset;

            void call(VM::Stack &stack) override;
        public:
            void get_refs(const std::function<void(Allocation *)> &callback) override;

            [[nodiscard]] FStr display() const override;

            BytecodeFunction(VM &vm, Module *mod, Scope *scope, Bytecode *bytecode, size_t offset = 0);
        };

        /**
         * CLass of native function value objects.
         */
        class NativeFunction final : public Function {
            std::function<void(VM::Stack &)> fn;

            void call(VM::Stack &stack) override;
        public:
            NativeFunction(VM &vm, Module *mod, decltype(fn) fn);

            void get_refs(const std::function<void(Allocation *)> &callback) override;

            [[nodiscard]] FStr display() const override;
        };

        /**
         * Structure that holds Funscript value of any type.
         */
        struct Value {
            union Data {
                fint num;
                fflp flp;
                Object *obj;
                Function *fun;
                fbln bln;
                String *str;
                Array *arr;
                Allocation *ptr;
            };
            Type type;
            Data data;

            Value(Type type = Type::INT, Data data = {.num = 0});

            void get_ref(const std::function<void(Allocation *)> &callback) const;
        };

        const Config config; // Configuration of current VM instance.
        MemoryManager mem; // Memory manager for the current VM.
    private:
        FMap<FStr, MemoryManager::AutoPtr<Module>> modules; // Loaded modules of this VM.
    public:

        explicit VM(Config config);

        void register_module(const funscript::FStr &name, funscript::VM::Module *mod);

        std::optional<Module *> get_module(const FStr &name);

        class StackOverflowError : std::exception {
        public:
            StackOverflowError();
        };

        /**
         * Class of Funscript execution stack.
         */
        class Stack final : public Allocation {
            void get_refs(const std::function<void(Allocation *)> &callback) override;
        public:
            using pos_t = ssize_t; // Type representing position in stack. Can be negative (-1 is the topmost element).

            Stack &operator=(const Stack &) = delete;
            Stack(const Stack &) = delete;

            /**
             * Creates new runnable execution stack.
             * @param vm The VM instance which owns the stack.
             * @param start The main function of this routine.
             */
            explicit Stack(VM &vm, Function *start);

            /**
             * Creates dead execution stack.
             * @param vm The VM instance which owns the stack.
             * @param start The main function of this routine.
             */
            explicit Stack(VM &vm);

            [[nodiscard]] pos_t size() const;
            const Value &operator[](pos_t pos) const; // Value stack indexing.

            static volatile std::sig_atomic_t kbd_int; // This flag is used to interrupt running execution stack.

            void exec_bytecode(Module *mod, Scope *scope, Bytecode *bytecode_obj, size_t offset, pos_t frame_start);

            void call_function(Function *fun);

            void call_operator(Operator op);
            void call_assignment();

            void execute();

            [[noreturn]] void
            panic(const std::string &msg, const std::source_location &loc = std::source_location::current());

            // Some functions for pushing values onto the value stack.

            void push_sep();
            void push_int(fint num);
            void push_flp(fflp flp);
            void push_obj(Object *obj);
            void push_fun(Function *fun);
            void push_str(String *str);
            void push_bln(fbln bln);
            void push_arr(Array *arr);
            void push_ptr(Allocation *ptr);

            /**
             * Discards values until (and including) the topmost separator.
             * @return `true` if any values were actually discarded.
             */
            bool discard();

            /**
             * Reverses values until the topmost separator.
             */
            void reverse();

            /**
             * Duplicates values until (and including) the topmost separator.
             */
            void duplicate();

            /**
             * Removes the topmost separator.
             */
            void remove();

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

            bool is_panicked() const;

            Frame *get_current_frame() const;

            template<typename Iter>
            void generate_stack_trace(Iter out) {
                for (Frame *frame = cur_frame; frame; frame = frame->prev_frame, out++) {
                    FStr row(vm.mem.str_alloc());
                    row += std::to_string(frame->depth) + ':';
                    row += " in ";
                    row += frame->fun->display();
                    row += " at ";
                    if (frame->meta_ptr->filename) row += frame->meta_ptr->filename;
                    else row += '?';
                    row += ':';
                    row += frame->meta_ptr->position.to_string();
                    *out = row;
                }
            }

            ~Stack() override;

        private:
            FVec<Value> values; // Values stack.
            Frame *cur_frame;

            bool panicked = false;

            /**
             * Pushes any value onto the value stack.
             * @param e Value to push.
             */
            void push(const Value &e);

            void op_panic(Operator op);

            /**
             * Returns mutable reference to the value stack element at the specified position.
             * @param pos Position to index.
             * @return The value at the specified position of value stack.
             */
            Value &get(pos_t pos);
        };

    private:
        class Panic {
        };
    };
}

#endif //FUNSCRIPT_VM_HPP
