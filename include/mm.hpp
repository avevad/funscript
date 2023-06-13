#ifndef FUNSCRIPT_MM_HPP
#define FUNSCRIPT_MM_HPP

#include <string>
#include <functional>
#include <deque>
#include <cstdint>

namespace funscript {

    /**
     * Interface for memory allocation and deallocation. Every instance of Funscript VM manages memory through this interface.
     */
    class Allocator {
    public:
        virtual void *allocate(size_t size) = 0;
        virtual void free(void *ptr, size_t size) noexcept = 0;
    };

    class MemoryManager;

    class VM;

    /**
     * Abstract class of every allocation managed by VM's MM.
     */
    class Allocation {
        friend MemoryManager;

        bool tracked = false;
        size_t mm_size = 0;
        size_t gc_pins = 0;

    public:
        VM &vm;

    protected:
        /**
        * Enumerates all the outgoing references to other allocations from this allocation.
        * @param callback The callback which should be called for every reference.
        */
        virtual void get_refs(const std::function<void(Allocation *)> &callback) = 0;

    public:
        explicit Allocation(VM &vm);

        virtual ~Allocation() = default;

        Allocation &operator=(const Allocation &) = delete;
        Allocation &operator=(const Allocation &&) = delete;
        Allocation(Allocation &) = delete;
        Allocation(Allocation &&) = delete;

    };

    class OutOfMemoryError final : std::bad_alloc {
    public:
        OutOfMemoryError() = default;
    };

    /**
     * Funscript allocator wrapper for C++ STL containers.
     * @tparam T The type which the allocator can allocate memory for.
     */
    template<typename T>
    class AllocatorWrapper {
    public:
        MemoryManager *mm;
        typedef T value_type;

        explicit AllocatorWrapper(MemoryManager *mem) : mm(mem) {}

        AllocatorWrapper(const AllocatorWrapper &old) : mm(old.mm) {}

        template<typename E>
        explicit AllocatorWrapper(const AllocatorWrapper<E> &old) : mm(old.mm) {}

        [[nodiscard]] T *allocate(size_t n);

        void deallocate(T *ptr, size_t n) noexcept;

        AllocatorWrapper &operator=(const AllocatorWrapper &old) {
            if (&old != this) mm = old.mm;
            return *this;
        }

        template<typename T1>
        bool operator==(const AllocatorWrapper<T1> &other) const {
            return mm == other.mm;
        }

        template<typename T1>
        bool operator!=(const AllocatorWrapper<T1> &other) const {
            return mm != other.mm;
        }
    };

    class MemoryManager final {
    public:
        const struct Config {
            Allocator *allocator = nullptr; // The allocator for this MM instance.
        } config;

    private:
        std::vector<Allocation *> gc_tracked; // Collection of all the allocation arrays tracked by the MM (and their sizes).

    public:
        /**
         * @tparam T Any type.
         * @return STL allocator for the specified type.
         */
        template<typename T>
        AllocatorWrapper<T> std_alloc() { return AllocatorWrapper<T>(this); }

        /**
         * @return STL string allocator.
         */
        AllocatorWrapper<char> str_alloc() { return std_alloc<char>(); }

        /**
         * Allocates memory for `n` values of type `T`
         * @tparam T Type of values to allocate memory for.
         * @param n Number of values.
         * @return Pointer to allocated memory.
         */
        template<typename T>
        T *allocate(size_t n = 1) {
            try {
                return reinterpret_cast<T *>(config.allocator->allocate(n * sizeof(T)));
            } catch (const OutOfMemoryError &e) {
                gc_cycle();
                return reinterpret_cast<T *>(config.allocator->allocate(n * sizeof(T)));
            }
        }

        /**
         * Frees previously `allocate`'d memory.
         * @param ptr Pointer to memory to deallocate.
         */
        void free(void *ptr, size_t size);

        MemoryManager(Config config);

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
            T *ptr = allocate<T>();
            try {
                new(ptr) T(std::forward<A>(args)...);
            } catch (...) {
                free(ptr, sizeof(T));
                throw;
            }
            gc_tracked.push_back(ptr);
            ptr->mm_size = sizeof(T);
            ptr->gc_pins++;
            ptr->tracked = true;
            return ptr;
        }

        /**
         * Looks for unused GC-tracked allocations and destroys them.
         */
        void gc_cycle();

        ~MemoryManager();

        /**
         * Class of smart allocation pointers which pin and unpin the allocation automatically.
         * @tparam A
         */
        template<typename A>
        class AutoPtr {
        private:
            A *alloc;
        public:
            AutoPtr(MemoryManager &mm, A *alloc) : alloc(alloc) {
                if (alloc) mm.gc_pin(alloc);
            }

            AutoPtr(AutoPtr &&other) noexcept: alloc(other.alloc) {
                other.alloc = nullptr;
            }

            AutoPtr &operator=(AutoPtr &&other) noexcept {
                if (&other != this) {
                    if (alloc) alloc->vm.mem.gc_unpin(alloc);
                    alloc = other.alloc;
                    other.alloc = nullptr;
                }
                return *this;
            }

            AutoPtr(AutoPtr &) = delete;
            AutoPtr &operator=(const AutoPtr &) = delete;

            A *get() const { return alloc; }

            void set(A *alloc1) {
                if (alloc) alloc->vm.mem.gc_unpin(alloc);
                alloc = alloc1;
                if (alloc) alloc->vm.mem.gc_pin(alloc);
            }

            A &operator*() const { return *alloc; }

            A *operator->() const { return alloc; }

            operator bool() const { return alloc != nullptr; }

            ~AutoPtr() {
                if (alloc) alloc->vm.mem.gc_unpin(alloc);
            }
        };

        /**
         * Constructs and pins a new GC-tracked allocation.
         * @tparam T Type of the allocation to create.
         * @tparam A Type of arguments to forward to `T`'s constructor.
         * @param args The arguments to forward to the constructor of the allocation.
         * @return The smart pointer to newly created GC-tracked allocation.
         */
        template<class T, typename... A>
        AutoPtr<T> gc_new_auto(A &&... args) {
            auto *alloc = gc_new<T, A...>(std::forward<A>(args)...);
            AutoPtr<T> ptr(*this, alloc);
            if (alloc) gc_unpin(alloc);
            return ptr;
        }
    };

    using FStr = std::basic_string<char, std::char_traits<char>, AllocatorWrapper<char>>;
    template<typename K, typename V>
    using FMap = std::unordered_map<K, V, std::hash<K>, std::equal_to<K>, AllocatorWrapper<std::pair<const K, V>>>;
    template<typename E>
    using FVec = std::vector<E, AllocatorWrapper<E>>;
    template<typename E>
    using FDeq = std::deque<E, AllocatorWrapper<E>>;

    template<typename T>
    T *AllocatorWrapper<T>::allocate(size_t n) {
        return mm->allocate<T>(n);
    }

    template<typename T>
    void AllocatorWrapper<T>::deallocate(T *ptr, size_t n) noexcept {
        mm->free(ptr, n * sizeof(T));
    }

    /**
     * Default Funscript allocator which uses C memory management functions.
     */
    class DefaultAllocator : public Allocator {
    private:
        size_t limit_bytes, used_bytes = 0;
    public:
        explicit DefaultAllocator(size_t limit_bytes = SIZE_MAX) : limit_bytes(limit_bytes) {}

        void *allocate(size_t size) override {
            if (limit_bytes - used_bytes < size) throw OutOfMemoryError();
            void *ptr = std::malloc(size);
            if (!ptr) throw OutOfMemoryError();
            used_bytes += size;
            return ptr;
        }

        void free(void *ptr, size_t size) noexcept override {
            if (ptr) used_bytes -= size;
            std::free(ptr);
        }
    };
}

#endif //FUNSCRIPT_MM_HPP