#ifndef FUNSCRIPT_MM_H
#define FUNSCRIPT_MM_H

#include <string>
#include <functional>
#include <deque>

namespace funscript {

    /**
     * Interface for memory allocation and deallocation. Every instance of Funscript VM manages memory through this interface.
     */
    class Allocator {
    public:
        virtual void *allocate(size_t size) = 0;
        virtual void free(void *ptr) = 0;
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

    class MemoryManager {
    public:
        const struct Config {
            Allocator *allocator = nullptr; // The allocator for this MM instance.
        } config;

    private:
        std::vector<Allocation *, AllocatorWrapper<Allocation *>> gc_tracked; // Collection of all the allocation arrays tracked by the MM (and their sizes).

    public:
        /**
         * @tparam T Any type.
         * @return STL allocator for the specified type.
         */
        template<typename T>
        AllocatorWrapper<T> std_alloc() { return AllocatorWrapper<T>(config.allocator); }

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
        T *allocate(size_t n = 1) { return reinterpret_cast<T *>(config.allocator->allocate(n * sizeof(T))); }

        /**
         * Frees previously `allocate`'d memory.
         * @param ptr Pointer to memory to deallocate.
         */
        void free(void *ptr);

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

        /**
         * Class of smart allocation pointers which pin and unpin the allocation automatically.
         * @tparam A
         */
        template<typename A>
        class AutoPtr {
        private:
            MemoryManager &mm;
            A *alloc;
        public:
            AutoPtr(MemoryManager &mm, A *alloc) : mm(mm), alloc(alloc) {
                if (alloc) mm.gc_pin(alloc);
            }

            AutoPtr(AutoPtr &&other) noexcept: mm(other.mm), alloc(other.alloc) {
                other.alloc = nullptr;
            }

            AutoPtr &operator=(AutoPtr &&other) noexcept {
                if (&other != this) {
                    if (alloc) mm.gc_unpin(alloc);
                    alloc = other.alloc;
                    other.alloc = nullptr;
                }
                return *this;
            }

            AutoPtr(AutoPtr &) = delete;
            AutoPtr &operator=(const AutoPtr &) = delete;

            A *get() const { return alloc; }

            void set(A *alloc1) {
                if (alloc) mm.gc_unpin(alloc);
                alloc = alloc1;
                if (alloc) mm.gc_pin(alloc);
            }

            A &operator*() const { return *alloc; }

            A *operator->() const { return alloc; }

            ~AutoPtr() {
                if (alloc) mm.gc_unpin(alloc);
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
            gc_unpin(alloc);
            return ptr;
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

    /**
     * Default Funscript allocator which uses C memory management functions.
     */
    class DefaultAllocator : public Allocator {
    public:
        void *allocate(size_t size) override { return std::malloc(size); }

        void free(void *ptr) override { std::free(ptr); }
    };
}

#endif //FUNSCRIPT_MM_H