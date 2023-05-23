#include <queue>
#include <unordered_set>
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
        if (!alloc->gc_pins && !alloc->gc_refc) {
            gc_deleted.push_back(alloc);
            alloc->~Allocation();
            free(alloc, alloc->mm_size);
        }
    }

    void MemoryManager::gc_add_ref(funscript::Allocation *alloc) {
        if (alloc->mm != this) [[unlikely]] assertion_failed("allocation is not tracked");
        alloc->gc_refc++;
    }

    void MemoryManager::gc_del_ref(funscript::Allocation *alloc) {
        if (alloc->mm != this) [[unlikely]] assertion_failed("allocation is not tracked");
        if (!alloc->gc_refc) [[unlikely]] assertion_failed("mismatched reference unregistering");
        alloc->gc_refc--;
        if (!alloc->gc_refc && !alloc->gc_pins) {
            gc_deleted.push_back(alloc);
            alloc->~Allocation();
            free(alloc, alloc->mm_size);
        }
    }

    void MemoryManager::gc_cycle() {
        { // Filter already deleted allocations
            std::unordered_set<Allocation *> gc_deleted_set(gc_deleted.begin(), gc_deleted.end());
            std::vector<Allocation *> gc_tracked_new;
            for (auto *alloc : gc_tracked) {
                if (!gc_deleted_set.contains(alloc)) gc_tracked_new.push_back(alloc);
            }
            gc_tracked = gc_tracked_new;
            gc_deleted.clear();
        }
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
                if (!ref->gc_refc) assertion_failed("allocation is reachable but has no references registered");
                ref->mm = this;
                queue.push(ref);
            });
        }
        {
            std::vector<Allocation *> gc_tracked_new;
            // Remove track of unreachable allocations and destroy them
            for (auto *alloc : gc_tracked) {
                if (alloc->mm) gc_tracked_new.push_back(alloc);
                else {
                    if (alloc->gc_refc) assertion_failed("allocation has a registered reference but is unreachable");
                    alloc->~Allocation();
                    free(alloc, alloc->mm_size);
                }
            }
            gc_tracked = gc_tracked_new;
        }
    }

    MemoryManager::~MemoryManager() {
        { // Filter already deleted allocations
            std::unordered_set<Allocation *> gc_deleted_set(gc_deleted.begin(), gc_deleted.end());
            std::vector<Allocation *> gc_tracked_new;
            for (auto *alloc : gc_tracked) {
                if (!gc_deleted_set.contains(alloc)) gc_tracked_new.push_back(alloc);
            }
            gc_tracked = gc_tracked_new;
            gc_deleted.clear();
        }
        for (auto *alloc : gc_tracked) {
            if (alloc->gc_pins) assertion_failed("destructing memory manager with pinned allocations");
            if (alloc->gc_refc) assertion_failed("allocation has a registered reference");
            alloc->~Allocation();
            free(alloc, alloc->mm_size);
        }
    }


}