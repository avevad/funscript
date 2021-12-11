//
// Created by avevad on 11/20/21.
//

#include "vm.h"
#include "common.h"

#include <stdexcept>
#include <cstring>
#include <utility>

namespace funscript {

    bool Object::contains(const fstring &key) {
        return str_map.contains(key);
    }

    Value &Object::var(const fstring &key) {
        return str_map[key];
    }

    Value &Scope::resolve(const fstring &key) const {
        if (!contains(key)) return vars->var(key);
        if (!vars->contains(key) && prev_scope != nullptr) return prev_scope->resolve(key);
        return vars->var(key);
    }

    bool Scope::contains(const fstring &key) const {
        if (vars->contains(key)) return true;
        return prev_scope != nullptr && prev_scope->contains(key);
    }

    stack_pos_t VM::Stack::size() const { return stack.size(); }

    VM::Stack::Stack(VM &vm) : vm(vm), stack(AllocatorWrapper<Value>(vm.config.alloc)) {}

    const Value &VM::Stack::operator[](stack_pos_t pos) { return get(pos); }

    void VM::Stack::push_sep() { push({Value::SEP}); }

    void VM::Stack::push_nul() { push({Value::NUL}); }

    void VM::Stack::push_int(int64_t num) { push({Value::INT, {.num = num}}); }

    Value VM::Stack::pop() {
        Value val = stack.back();
        stack.pop_back();
        return val;
    }

    void VM::Stack::pop(stack_pos_t pos) {
        if (pos < 0) pos += size();
        stack.resize(pos);
    }

    void VM::Stack::call(Function *fun, Frame *frame) {
        auto *new_frame = vm.mem.gc_new<Frame>(frame);
        (*fun)(*this, frame);
    }

    VM::Stack::~Stack() = default;

    void VM::Stack::push(const Value &e) { stack.push_back(e); }

    Value &VM::Stack::get(stack_pos_t pos) {
        if (pos < 0) pos += size();
        return stack[pos];
    }

    stack_pos_t VM::Stack::find_sep(stack_pos_t before) {
        stack_pos_t pos = before - 1;
        while (get(pos).type != Value::SEP) pos--;
        return pos;
    }

    void VM::Stack::mov(bool discard) {
        stack_pos_t ref_sep = abs(find_sep(0)), val_sep = abs(find_sep(ref_sep));
        stack_pos_t ref_pos = ref_sep + 1, val_pos = val_sep + 1;
        while (get(val_pos).type != Value::SEP && ref_pos != size()) *get(ref_pos++).data.ref = get(val_pos++);
        if (discard) {
            pop(val_sep);
        } else {
            pop(ref_sep);
            memmove(stack.data() + val_sep, stack.data() + val_sep + 1, sizeof(Value) * (size() - val_sep - 1));
            stack.pop_back();
        }
    }

    void VM::Stack::dis() {
        stack_pos_t sep_pos = find_sep(0);
        pop(sep_pos);
    }

    void VM::Stack::op(Frame *frame, Operator op) {
        stack_pos_t pos_b = find_sep() + 1, pos_a = find_sep(pos_b - 1) + 1;
        size_t cnt_b = 0 - pos_b, cnt_a = pos_b - pos_a - 1;
        if (op == Operator::CALL) {
            FS_ASSERT(cnt_b == 1); // TODO
            FS_ASSERT(get(-1).type == Value::FUN); // TODO
            Function *fun = pop().data.fun;
            pop();
            call(fun, frame);
            return;
        }
        if (cnt_a == 0) {
            FS_ASSERT(cnt_b == 1); // TODO
            FS_ASSERT(get(pos_b).type == Value::INT); // TODO
            int64_t val = get(-1).data.num;
            int64_t result;
            switch (op) {
                case Operator::PLUS:
                    result = val;
                    break;
                case Operator::MINUS:
                    result = -val;
                    break;
                default:
                    assert_failed("invalid operator"); // TODO
            }
            pop(-3);
            push_int(result);
            return;
        }
        if (cnt_a == 1) {
            FS_ASSERT(get(pos_a).type == Value::INT); // TODO
            FS_ASSERT(cnt_b == 1 && get(pos_b).type == Value::INT); // TODO
            int64_t left = get(-3).data.num, right = get(-1).data.num;
            int64_t result;
            switch (op) {
                case Operator::TIMES:
                    result = left * right;
                    break;
                case Operator::DIVIDE:
                    result = left / right;
                    break;
                case Operator::PLUS:
                    result = left + right;
                    break;
                case Operator::MINUS:
                    result = left - right;
                    break;
                case Operator::MODULO:
                    result = left % right;
                    break;
                default:
                    assert_failed("invalid operator");
            }
            pop(-4);
            push_int(result);
            return;
        }
        assert_failed("invalid number of values"); // TODO
    }

    void VM::Stack::push_ref(Scope *scope, const fstring &key) {
        push({.type = Value::REF, .data = {.ref = &scope->resolve(key)}});
    }

    void VM::Stack::push_val(Scope *scope, const fstring &key) {
        push(scope->resolve(key));
    }

    stack_pos_t VM::Stack::abs(stack_pos_t pos) const { return pos < 0 ? size() + pos : pos; }

    void VM::Stack::push_fun(Function *fun) {
        push({.type=Value::FUN, .data = {.fun = fun}});
    }

    void VM::Stack::push_object(Object *object) {
        push({.type = Value::OBJ, .data = {.obj = object}});
    }

    VM::VM(VM::Config config) : config(config), stacks(AllocatorWrapper<Stack *>(config.alloc)), mem(*this) {}

    VM::Stack &VM::stack(size_t id) { return *stacks[id]; }

    size_t VM::new_stack() {
        stacks.push_back(new(mem.allocate<Stack>()) Stack(*this));
        return stacks.size() - 1;
    }

    VM::~VM() {
        for (Stack *stack: stacks) {
            stack->~Stack();
            mem.free(stack);
        }
    }

    void VM::Stack::exec_bytecode(Frame *frame, Scope *scope, Holder<char> *bytecode_hld, size_t offset) {
        const char *bytecode = bytecode_hld->data + offset;
        size_t ip = 0;
        while (true) {
            auto opcode = (Opcode) bytecode[ip];
            switch (opcode) {
                case Opcode::NOP:
                    break;
                case Opcode::NUL: {
                    ip++;
                    push_nul();
                    break;
                }
                case Opcode::SEP: {
                    ip++;
                    push_sep();
                    break;
                }
                case Opcode::INT: {
                    ip++;
                    int64_t num;
                    memcpy(&num, bytecode + ip, sizeof(int64_t));
                    ip += sizeof(int64_t);
                    push_int(num);
                    break;
                }
                case Opcode::OP: {
                    ip++;
                    auto oper = (Operator) bytecode[ip];
                    ip++;
                    op(frame, oper);
                    break;
                }
                case Opcode::REF: {
                    ip++;
                    ssize_t pos = 0;
                    memcpy(&pos, bytecode + ip, sizeof(ssize_t));
                    ip += sizeof(ssize_t);
                    fstring key(reinterpret_cast<const wchar_t *>(bytecode + pos), vm.mem.str_alloc());
                    push_ref(scope, key);
                    break;
                }
                case Opcode::VAL: {
                    ip++;
                    ssize_t pos = 0;
                    memcpy(&pos, bytecode + ip, sizeof(ssize_t));
                    ip += sizeof(ssize_t);
                    fstring key(reinterpret_cast<const wchar_t *>(bytecode + pos), vm.mem.str_alloc());
                    push_val(scope, key);
                    break;
                }
                case Opcode::MOV: {
                    ip++;
                    mov();
                    break;
                }
                case Opcode::DIS: {
                    ip++;
                    dis();
                    break;
                }
                case Opcode::NS: {
                    ip++;
                    auto *vars = vm.mem.gc_new<Object>(vm);
                    scope = vm.mem.gc_new<Scope>(vars, scope);
                    break;
                }
                case Opcode::DS: {
                    ip++;
                    scope = scope->prev_scope;
                    break;
                }
                case Opcode::END:
                    return;
                case Opcode::FUN: {
                    ip++;
                    ssize_t pos = 0;
                    memcpy(&pos, bytecode + ip, sizeof(ssize_t));
                    ip += sizeof(ssize_t);
                    push_fun(vm.mem.gc_new<CompiledFunction>(scope, bytecode_hld, offset + pos));
                    break;
                }
                case Opcode::MVD: {
                    ip++;
                    mov(true);
                    break;
                }
                case Opcode::OBJ: {
                    ip++;
                    push_object(scope->vars);
                    break;
                }
                default:
                    throw std::runtime_error("unknown opcode");
            }
        }
    }

    void VM::MemoryManager::gc_track(VM::Allocation *alloc) {
        if (gc_tracked.contains(alloc)) throw std::runtime_error("allocation is already tracked");
        gc_tracked.insert(alloc);
    }

    VM::MemoryManager::~MemoryManager() {
        for (Allocation *alloc: gc_tracked) {
            (*alloc).~Allocation();
            free(alloc);
        }
    }
}