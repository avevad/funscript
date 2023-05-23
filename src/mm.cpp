#include <queue>
#include "mm.h"
#include "common.h"

namespace funscript {
    MemoryManager::MemoryManager(MemoryManager::Config config) : config(config) {}

    void MemoryManager::free(void *ptr, size_t size) { config.allocator->free(ptr, size); }

    void MemoryManager::gc_pin(Allocation *alloc) {
        if (alloc->mm != this) [[unlikely]] assertion_failed("allocation is not tracked");
        alloc->gc_pins++;
    }

    void MemoryManager::gc_unpin(Allocation *alloc) {
        if (alloc->mm != this) [[unlikely]] assertion_failed("allocation is not tracked");
        if (!alloc->gc_pins) [[unlikely]] assertion_failed("mismatched allocation unpin");
        alloc->gc_pins--;
    }

    void MemoryManager::gc_cycle() {
        std::queue<Allocation *> queue;
        // Populate the queue with GC roots, unmark other allocations
        for (auto *alloc : gc_tracked) {
            if (alloc->gc_pins) queue.push(alloc);
            else alloc->mm = nullptr;
        }
        // Using BFS to mark all reachable allocations
        while (!queue.empty()) {
            auto *alloc = queue.front();
            queue.pop();
            alloc->get_refs([&queue, this](Allocation *ref) -> void {
                if (!ref || ref->mm) return;
                ref->mm = this;
                queue.push(ref);
            });
        }
        std::vector<Allocation *> gc_tracked_new;
        // Remove track of unreachable allocations and destroy them
        for (auto *alloc : gc_tracked) {
            if (alloc->mm) gc_tracked_new.push_back(alloc);
            else {
                alloc->~Allocation();
                free(alloc, alloc->mm_size);
            }
        }
        gc_tracked = gc_tracked_new;
    }

    MemoryManager::~MemoryManager() {
        for (auto *alloc : gc_tracked) {
            if (alloc->gc_pins) assertion_failed("destructing memory manager with pinned allocations");
            alloc->~Allocation();
            free(alloc, alloc->mm_size);
        }
    }
}