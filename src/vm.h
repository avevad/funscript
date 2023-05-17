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
#include "common.h"

namespace funscript {

    /**
     * Interface for memory allocation and deallocation. Every instance of Funscript VM manages memory through this interface.
     */
    class Allocator {
    public:
        virtual void *allocate(size_t size) = 0;
        virtual void free(void *ptr) = 0;
    };

    /**
     * Default Funscript allocator which uses C memory management functions.
     */
    class DefaultAllocator : public Allocator {
    public:
        void *allocate(size_t size) override { return std::malloc(size); }

        void free(void *ptr) override { std::free(ptr); }
    };

    /**
     * Funscript allocator wrapper for C++ STL containers.
     * @tparam T The type which the allocator can allocate memory for.
     */
    template<typename T>
    class AllocatorWrapper {
    public:
        Allocator *alloc;
        typedef T value_type;

        explicit AllocatorWrapper(Allocator *alloc) : alloc(alloc) {}

        AllocatorWrapper(const AllocatorWrapper &old) : alloc(old.alloc) {}

        template<typename E>
        explicit AllocatorWrapper(const AllocatorWrapper<E> &old) : alloc(old.alloc) {}

        [[nodiscard]] T *allocate(size_t n) { return reinterpret_cast<T *>(alloc->allocate(sizeof(T) * n)); }

        void deallocate(T *ptr, size_t n) noexcept { alloc->free(ptr); }

        AllocatorWrapper &operator=(const AllocatorWrapper &old) {
            if (&old != this) alloc = old.alloc;
            return *this;
        }

        template<typename T1>
        bool operator==(const AllocatorWrapper<T1> &other) const {
            return true;
        }

        template<typename T1>
        bool operator!=(const AllocatorWrapper<T1> &other) const {
            return false;
        }
    };

    using fstr = std::basic_string<char, std::char_traits<char>, AllocatorWrapper<char>>;
    template<typename K, typename V>
    using fumap = std::unordered_map<K, V, std::hash<K>, std::equal_to<K>, AllocatorWrapper<std::pair<const K, V>>>;
    template<typename E>
    using fvec = std::vector<E, AllocatorWrapper<E>>;
    template<typename E>
    using fdeq = std::deque<E, AllocatorWrapper<E>>;
    using fint = int64_t;

    class VM {
    public:
        VM(const VM &vm) = delete;
        VM &operator=(const VM &vm) = delete;

        struct Config {
            Allocator *allocator = nullptr; // The allocator for this VM instance.
        };

        class MemoryManager;

        /**
         * Abstract class of every allocation managed by VM's MM.
         */
        class Allocation {
            friend MemoryManager;

            size_t gc_pins = 0;
            MemoryManager *mm = nullptr;

            /**
             * Enumerates all the outgoing references to other allocations from this allocation.
             * @param callback The callback which should be called for every reference.
             */
            virtual void get_refs(const std::function<void(Allocation *)> &callback) = 0;
        public:
            virtual ~Allocation() = default;
        };

        class Stack;

        struct Value;

        /**
         * Class of array value objects.
         */
        class Array : public VM::Allocation {
            fvec<Value> values;
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
        class String : public VM::Allocation {
            void get_refs(const std::function<void(Allocation *)> &callback) override;
        public:
            const fstr bytes;

            explicit String(fstr bytes);
        };

        /**
         * Class of error value objects.
         */
        class Error : public VM::Allocation {
            void get_refs(const std::function<void(Allocation *)> &callback) override;
        public:
            const fstr desc; // Human-readable description of the error.

            explicit Error(fstr desc);
        };

        /**
         * Class of object value objects.
         */
        class Object : public VM::Allocation {
        private:
            fumap<fstr, Value> fields; // Dictionary of object's fields.

            void get_refs(const std::function<void(Allocation *)> &callback) override;
        public:
            VM &vm;

            explicit Object(VM &vm) : vm(vm), fields(vm.mem.std_alloc<std::pair<const fstr, Value>>()) {};

            [[nodiscard]] bool contains_field(const fstr &key) const;
            [[nodiscard]] std::optional<Value> get_field(const fstr &key) const;
            void set_field(const fstr &key, Value val);

            ~Object() override = default;
        };

        /**
         * Class which represents the scope of an expression. Can be nested, in such case it contains the pointer to the parent scope.
         */
        class Scope : public VM::Allocation {
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

            Scope(Object *vars, Scope *prev_scope) : vars(vars), prev_scope(prev_scope) {};

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
        };

        class Function;

        /**
         * Objects of this class hold information about VM stack frame.
         */
        class Frame : public VM::Allocation {
            friend VM::Stack;
            Function *cont_fn; // The function to be called in this frame.

            void get_refs(const std::function<void(Allocation *)> &callback) override;
        public:
            explicit Frame(Function *cont_fn);
        };

        /**
         * Class of function value objects.
         */
        class Function : public VM::Allocation {
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
        class Bytecode : public VM::Allocation {
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

            void get_ref(const std::function<void(VM::Allocation *)> &callback) const {
                if (type == Type::OBJ) callback(data.obj);
                if (type == Type::FUN) callback(data.fun);
                if (type == Type::STR) callback(data.str);
                if (type == Type::ERR) callback(data.err);
                if (type == Type::ARR) callback(data.arr);
            }
        };

        class MemoryManager {
        public:
            VM &vm; // Funscript VM instance which owns this memory manager.
        private:
            fvec<Allocation *> gc_tracked; // Collection of all the allocation arrays tracked by the MM (and their sizes).
        public:

            /**
             * @tparam T Any type.
             * @return STL allocator for the specified type.
             */
            template<typename T>
            AllocatorWrapper<T> std_alloc() { return AllocatorWrapper<T>(vm.config.allocator); }

            /**
             * @return STL string allocator.
             */
            AllocatorWrapper<fstr::value_type> str_alloc() { return std_alloc<fstr::value_type>(); }

            /**
             * Allocates memory for `n` values of type `T`
             * @tparam T Type of values to allocate memory for.
             * @param n Number of values.
             * @return Pointer to allocated memory.
             */
            template<typename T>
            T *allocate(size_t n = 1) { return reinterpret_cast<T *>(vm.config.allocator->allocate(n * sizeof(T))); }

            /**
             * Frees previously `allocate`'d memory.
             * @param ptr Pointer to memory to deallocate.
             */
            void free(void *ptr);

            explicit MemoryManager(VM &vm);

            /**
             * Pins GC-tracked allocation.
             * @param alloc The allocation to pin.
             */
            void gc_pin(Allocation *alloc);

            /**
             * Unpins GC-tracked allocation.
             * @param alloc The allocation to unpin.
             */
            void gc_unpin(Allocation *alloc);

            /**
             * Constructs and pins a new GC-tracked allocation.
             * @tparam T Type of the allocation to create.
             * @tparam A Type of arguments to forward to `T`'s constructor.
             * @param args The arguments to forward to the constructor of the allocation.
             * @return The pointer to newly created GC-tracked allocation.
             */
            template<class T, typename... A>
            T *gc_new(A &&... args) {
                T *ptr = new(allocate<T>()) T(std::forward<A>(args)...);
                gc_tracked.push_back(ptr);
                ptr->gc_pins++;
                ptr->mm = this;
                return ptr;
            }

            /**
             * Looks for unused GC-tracked allocations and destroys them.
             */
            void gc_cycle();

            ~MemoryManager();
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

            void exec_bytecode(Scope *scope, Bytecode *bytecode_obj, size_t offset, pos_t frame_start);
            void call_operator(Operator op, Function *cont_fn);
            void call_assignment(Function *cont_fn);
            void call_function(Function *fun, Function *cont_fn);
            void continue_execution();

            // Some functions for pushing values onto the value stack.

            void push_sep();
            void push_nul();
            void push_int(fint num);
            void push_obj(Object *obj);
            void push_fun(Function *fun);
            void push_str(String *str);
            void push_bln(bool bln);
            void push_err(Error *err);
            void push_arr(Array *arr);

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
            void push(const Value &e);

            /**
             * Returns mutable reference to the value stack element at the specified position.
             * @param pos Position to index.
             * @return The value at the specified position of value stack.
             */
            Value &get(pos_t pos);
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
