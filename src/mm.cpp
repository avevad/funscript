#include "mm.hpp"
#include "common.hpp"

#include <queue>

namespace funscript {
    Allocation::Allocation(funscript::VM &vm) : vm(vm) {}

    MemoryManager::MemoryManager(MemoryManager::Config config) : config(config) {}

    void MemoryManager::free(void *ptr, size_t size) {
        config.allocator->free(ptr, size);
    } // NOLINT(readability-make-member-function-const)

    void MemoryManager::gc_pin(Allocation *alloc) { // NOLINT(readability-convert-member-functions-to-static)
        if (!alloc->tracked) [[unlikely]] assertion_failed("allocation is not tracked");
        alloc->gc_pins++;
    }

    void MemoryManager::gc_unpin(Allocation *alloc) { // NOLINT(readability-convert-member-functions-to-static)
        if (!alloc->tracked) [[unlikely]] assertion_failed("allocation is not tracked");
        if (!alloc->gc_pins) [[unlikely]] assertion_failed("mismatched allocation unpin");
        alloc->gc_pins--;
    }

    void MemoryManager::gc_cycle() {
        std::queue<Allocation *> queue;
        // Populate the queue with GC roots, unmark other allocations
        for (auto *alloc : gc_tracked) {
            if (alloc->gc_pins) queue.push(alloc);
            else alloc->tracked = false;
        }
        // Using BFS to mark all reachable allocations
        while (!queue.empty()) {
            auto *alloc = queue.front();
            queue.pop();
            alloc->get_refs([&queue](Allocation *ref) -> void {
                if (!ref || ref->tracked) return;
                ref->tracked = true;
                queue.push(ref);
            });
        }
        std::vector<Allocation *> gc_tracked_new;
        // Remove track of unreachable allocations and destroy them
        for (auto *alloc : gc_tracked) {
            if (alloc->tracked) gc_tracked_new.push_back(alloc);
            else {
                size_t sz = alloc->mm_size;
                alloc->~Allocation();
                free(alloc, sz);
            }
        }
        gc_tracked = gc_tracked_new;
    }

    MemoryManager::~MemoryManager() {
        for (auto *alloc : gc_tracked) {
            if (alloc->gc_pins) assertion_failed("destructing memory manager with pinned allocations");
            size_t sz = alloc->mm_size;
            alloc->~Allocation();
            free(alloc, sz);
        }
    }
}