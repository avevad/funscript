#include <memory>
#include "vm.hpp"
#include "native.hpp"

using namespace funscript;

void fd_write(VM::Stack &stack, VM::Frame *frame) {
    std::function fn([](fint fd, MemoryManager::AutoPtr<VM::Array> bytes, fint pos, fint len) -> fint {
        if (pos < 0 || len < 0 || pos + len > bytes->len()) throw NativeError("invalid range");
        auto buf = std::make_unique<uint8_t[]>(len);
        for (size_t buf_pos = 0; buf_pos < len; buf_pos++) {
            auto val = (*bytes)[pos + buf_pos];
            if (val.type != Type::INT || val.data.num < 0 || 255 < val.data.num) {
                throw NativeError("invalid value in bytes array");
            }
            *(buf.get() + buf_pos) = val.data.num;
        }
        return write(int(fd), buf.get(), len);
    });
    call_native_function(stack.vm, stack, frame, fn);
}