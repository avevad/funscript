#include "vm.hpp"
#include "utils.hpp"

#include <memory>
#include <cstring>

namespace funscript::stdlib {

    namespace lang {

        void panic(VM::Stack &stack) {
            std::function fn([&stack](MemoryManager::AutoPtr<VM::String> msg) -> void {
                stack.panic(std::string(msg->bytes));
            });
            util::call_native_function(stack, fn);
        }

        void is_object(VM::Stack &stack) {
            fbln result = stack[-1].type == Type::OBJ && stack[-2].type == Type::SEP;
            stack.pop(stack.find_sep());
            stack.push_bln(result);
        }

        void is_integer(VM::Stack &stack) {
            fbln result = stack[-1].type == Type::INT && stack[-2].type == Type::SEP;
            stack.pop(stack.find_sep());
            stack.push_bln(result);
        }

        void is_string(VM::Stack &stack) {
            fbln result = stack[-1].type == Type::STR && stack[-2].type == Type::SEP;
            stack.pop(stack.find_sep());
            stack.push_bln(result);
        }

        void is_array(VM::Stack &stack) {
            fbln result = stack[-1].type == Type::ARR && stack[-2].type == Type::SEP;
            stack.pop(stack.find_sep());
            stack.push_bln(result);
        }

        void is_boolean(VM::Stack &stack) {
            fbln result = stack[-1].type == Type::BLN && stack[-2].type == Type::SEP;
            stack.pop(stack.find_sep());
            stack.push_bln(result);
        }

        void is_float(VM::Stack &stack) {
            fbln result = stack[-1].type == Type::FLP && stack[-2].type == Type::SEP;
            stack.pop(stack.find_sep());
            stack.push_bln(result);
        }

        void is_function(VM::Stack &stack) {
            fbln result = stack[-1].type == Type::FUN && stack[-2].type == Type::SEP;
            stack.pop(stack.find_sep());
            stack.push_bln(result);
        }

        void is_pointer(VM::Stack &stack) {
            fbln result = stack[-1].type == Type::PTR && stack[-2].type == Type::SEP;
            stack.pop(stack.find_sep());
            stack.push_bln(result);
        }

        void fun_to_str(VM::Stack &stack) {
            std::function fn([](MemoryManager::AutoPtr<VM::Function> fun) -> MemoryManager::AutoPtr<VM::String> {
                return fun->vm.mem.gc_new_auto<VM::String>(fun->vm, fun->display());
            });
            util::call_native_function(stack, fn);
        }

        void ptr_to_str(VM::Stack &stack) {
            std::function fn([](MemoryManager::AutoPtr<Allocation> ptr) -> MemoryManager::AutoPtr<VM::String> {
                return ptr->vm.mem.gc_new_auto<VM::String>(ptr->vm, FStr(
                        "pointer(" + addr_to_string(ptr.get()) + ")", ptr->vm.mem.str_alloc()
                ));
            });
            util::call_native_function(stack, fn);
        }

        void int_to_str(VM::Stack &stack) {
            std::function fn([&stack](fint num) -> MemoryManager::AutoPtr<VM::String> {
                return stack.vm.mem.gc_new_auto<VM::String>(stack.vm, FStr(
                        std::to_string(num), stack.vm.mem.str_alloc()
                ));
            });
            util::call_native_function(stack, fn);
        }

        void flp_to_str(VM::Stack &stack) {
            std::function fn([&stack](fflp flp) -> MemoryManager::AutoPtr<VM::String> {
                return stack.vm.mem.gc_new_auto<VM::String>(stack.vm, FStr(
                        std::to_string(flp), stack.vm.mem.str_alloc()
                ));
            });
            util::call_native_function(stack, fn);
        }

        void str_to_str(VM::Stack &stack) {
            std::function fn([](MemoryManager::AutoPtr<VM::String> str) -> MemoryManager::AutoPtr<VM::String> {
                size_t length = 0;
                length++;
                for (char c : str->bytes) {
                    if (c != '\'' && std::isprint(c)) length++;
                    else length += 4; // '\xNN'
                }
                length++;
                FStr result(str->vm.mem.str_alloc());
                result.reserve(length);
                result += '\'';
                for (char c : str->bytes) {
                    if (c != '\'' && std::isprint(c)) result += char(c);
                    else {
                        result += '\\';
                        result += 'x';
                        int code = (unsigned char) c;
                        int high = code >> 4;
                        int low = code & 0xF;
                        result += high < 10 ? char('0' + high) : char('a' + high - 10);
                        result += low < 10 ? char('0' + low) : char('a' + low - 10);
                    }
                }
                result += '\'';
                return str->vm.mem.gc_new_auto<VM::String>(str->vm, result);
            });
            util::call_native_function(stack, fn);
        }

        namespace {

            VM::Frame *get_caller(VM::Stack &stack, ssize_t pos) {
                VM::Frame *frame = stack.get_current_frame();
                while (pos != 0) {
                    frame = frame->prev_frame;
                    pos++;
                }
                return frame;
            }

            void strip_submodule_name(FStr &mod_name) {
                auto pos = mod_name.rfind('.');
                if (pos == std::string::npos) {
                    mod_name = "";
                    return;
                }
                mod_name.resize(pos);
            }

        }

        void module_(VM::Stack &stack) {
            std::function fn([&stack](MemoryManager::AutoPtr<VM::String> alias_val) ->
                                     MemoryManager::AutoPtr<VM::Object> {
                FStr alias = alias_val->bytes;
                if (alias.empty()) stack.panic("module alias cannot be empty");
                if (std::count(alias.begin() + 1, alias.end(), '.')) {
                    stack.panic("invalid characters in module alias");
                }
                auto *caller = get_caller(stack, -2);
                auto mod = caller->fun->mod->get_dependency(alias);
                if (!mod.has_value()) stack.panic("dependency is not registered: " + std::string(alias));
                auto exports = mod.value()->object->get_field(MODULE_EXPORTS_VAR);
                if (!exports.has_value() || exports.value().type != Type::OBJ) {
                    stack.panic("module has no exports: " + std::string(alias));
                }
                return MemoryManager::AutoPtr(exports->data.obj);
            });
            util::call_native_function(stack, fn);
        }

        void submodule(VM::Stack &stack) {
            std::function fn([&stack](MemoryManager::AutoPtr<VM::String> alias_val) ->
                                     MemoryManager::AutoPtr<VM::Object> {
                FStr alias = alias_val->bytes;
                if (alias.empty()) stack.panic("module alias cannot be empty");
                if (std::count(alias.begin(), alias.end(), '.')) {
                    stack.panic("invalid characters in module alias");
                }
                alias = '.' + alias;
                auto *caller = get_caller(stack, -2);
                FStr prefix = caller->fun->mod->name;
                while (!prefix.empty()) {
                    FStr mod_name = prefix + alias;
                    auto mod = stack.vm.get_module(mod_name);
                    if (mod.has_value()) {
                        auto exports = mod.value()->object->get_field(MODULE_EXPORTS_VAR);
                        if (!exports.has_value() || exports.value().type != Type::OBJ) {
                            stack.panic("module has no exports: " + std::string(mod_name));
                        }
                        return MemoryManager::AutoPtr(exports->data.obj);
                    }
                    strip_submodule_name(prefix);
                }
                stack.panic("failed to find submodule: " + std::string(alias));
            });
            util::call_native_function(stack, fn);
        }

        void import_(VM::Stack &stack) {
            std::function fn([&stack](MemoryManager::AutoPtr<VM::Object> obj) -> void {
                auto *caller_scope = get_caller(stack, -2)->get_meta().scope;
                for (const auto &[var, val] : obj->get_fields()) {
                    caller_scope->vars->set_field(var, val);
                }
            });
            util::call_native_function(stack, fn);
        }

        void bytes_allocate(VM::Stack &stack) {
            std::function fn([&stack](fint size) -> MemoryManager::AutoPtr<Allocation> {
                auto ptr = stack.vm.mem.gc_new_auto_arr(stack.vm, size_t(size), char(0));
                return MemoryManager::AutoPtr<Allocation>(ptr.get());
            });
            util::call_native_function(stack, fn);
        }

        void bytes_paste_from_string(VM::Stack &stack) {
            std::function fn([](MemoryManager::AutoPtr<Allocation> data,
                                fint pos, MemoryManager::AutoPtr<VM::String> str, fint beg, fint end) -> void {
                char *bytes = dynamic_cast<ArrayAllocation<char> *>(data.get())->data();
                std::memcpy(bytes + pos, str->bytes.c_str() + beg, end - beg);
            });
            util::call_native_function(stack, fn);
        }

        void concat(VM::Stack &stack) {
            size_t length = 0;
            VM::Stack::pos_t pos = -1;
            while (stack[pos].type != Type::SEP) {
                if (stack[pos].type != Type::STR) stack.panic("strings expected");
                length += stack[pos].data.str->bytes.size();
                pos--;
            }
            auto end = pos;
            FStr str(stack.vm.mem.str_alloc());
            str.reserve(length);
            for (pos = end + 1; pos != 0; pos++) str.append(stack[pos].data.str->bytes);
            stack.pop(end);
            stack.push_str(stack.vm.mem.gc_new_auto<VM::String>(stack.vm, str).get());
        }

    }

    namespace sys {

        void posix_get_errno(VM::Stack &stack) {
            std::function fn([]() -> fint { return fint(errno); });
            util::call_native_function(stack, fn);
        }

        void posix_write(VM::Stack &stack) {
            std::function fn([](fint fd, MemoryManager::AutoPtr<Allocation> data, fint beg, fint end) -> fint {
                char *bytes = dynamic_cast<ArrayAllocation<char> *>(data.get())->data();
                return fint(write(int(fd), bytes + beg, size_t(end - beg)));
            });
            util::call_native_function(stack, fn);
        }

        void posix_read(VM::Stack &stack) {
            std::function fn([](fint fd, MemoryManager::AutoPtr<Allocation> data, fint beg, fint end) -> fint {
                char *bytes = dynamic_cast<ArrayAllocation<char> *>(data.get())->data();
                return fint(read(int(fd), bytes + beg, size_t(end - beg)));
            });
            util::call_native_function(stack, fn);
        }

        void posix_strerror(VM::Stack &stack) {
            std::function fn([&stack](fint err_num) -> MemoryManager::AutoPtr<VM::String> {
                FStr err_str(strerror(int(err_num)), stack.vm.mem.str_alloc());
                return stack.vm.mem.gc_new_auto<VM::String>(stack.vm, err_str);
            });
            util::call_native_function(stack, fn);
        }
    }

}