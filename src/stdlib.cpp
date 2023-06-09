#include "vm.hpp"

using namespace funscript;

void fd_write(VM::Stack &stack, VM::Frame *frame) {
    auto frame_start = stack.find_sep();
    stack.reverse();
    auto fd_val = stack[-1];
    if (fd_val.type != Type::INT) {
        return stack.raise_err("argument #1 (`fd`) must be an integer", frame_start);
    }
    int fd = int(fd_val.data.num);
    auto bytes_val = stack[-2];
    if (bytes_val.type != Type::ARR) {
        return stack.raise_err("argument #2 (`bytes`) must be an array", frame_start);
    }
    auto bytes = MemoryManager::AutoPtr<VM::Array>(stack.vm.mem, bytes_val.data.arr);
    auto pos_val = stack[-3];
    if (pos_val.type != Type::INT) {
        return stack.raise_err("argument #3 (`pos`) must be an integer", frame_start);
    }
    auto pos = pos_val.data.num;
    auto len_val = stack[-4];
    if (len_val.type != Type::INT) {
        return stack.raise_err("argument #4 (`len`) must be an integer", frame_start);
    }
    auto len = len_val.data.num;
    if (pos < 0 || len < 0 || pos + len > bytes->len()) {
        return stack.raise_err("invalid range", frame_start);
    }
    auto *buf = new uint8_t[len];
    for (size_t buf_pos = 0; buf_pos < len; buf_pos++) {
        auto val = (*bytes)[pos + buf_pos];
        if (val.type != Type::INT || val.data.num < 0 || 255 < val.data.num) {
            delete[] buf;
            return stack.raise_err("invalid value in bytes array", frame_start);
        }
        buf[buf_pos] = val.data.num;
    }
    ssize_t res = write(fd, buf, len);
    delete[] buf;
    stack.pop(frame_start);
    stack.push_int(res);
}
