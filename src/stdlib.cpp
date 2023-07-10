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
                std::memcpy(bytes, str->bytes.c_str() + beg, end - beg);
            });
            util::call_native_function(stack, fn);
        }

    }

}