#include "vm.hpp"
#include "utils.hpp"

#include <memory>

namespace funscript::stdlib {

    void str_to_bytes(VM::Stack &stack) {
        std::function fn([&stack](MemoryManager::AutoPtr<VM::String> str) -> MemoryManager::AutoPtr<Allocation> {
            auto ptr = stack.vm.mem.gc_new_auto_arr<char>(stack.vm, str->bytes.size(), '\0');
            std::copy(str->bytes.data(), str->bytes.data() + str->bytes.size(), ptr->data());
            return MemoryManager::AutoPtr<Allocation>(ptr.get());
        });
        util::call_native_function(stack.vm, stack, fn);
    }

    namespace io {
        void fd_write(VM::Stack &stack) {
            std::function fn([&stack](fint fd, MemoryManager::AutoPtr<Allocation> bytes) -> fint {
                auto ptr = MemoryManager::AutoPtr<ArrayAllocation<char>>(dynamic_cast<ArrayAllocation<char> *>(bytes.get()));
                if (!ptr) stack.panic("invalid pointer");
                return fint(write(int(fd), ptr->data(), ptr->size()));
            });
            util::call_native_function(stack.vm, stack, fn);
        }
    }

}