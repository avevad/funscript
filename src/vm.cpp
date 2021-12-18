//
// Created by avevad on 11/20/21.
//

#include "vm.h"
#include "common.h"

#include <stdexcept>
#include <cstring>
#include <utility>

namespace funscript {

    bool Object::contains(const fstring &key) const {
        return str_map.contains(key);
    }

    Value Object::get_val(const fstring &key) const {
        return str_map.at(key);
    }

    void Object::get_refs(const std::function<void(Allocation * )> &callback) {
        for (const auto &[key, val]: str_map) {
            if (val.type == Value::OBJ) callback(val.data.obj);
            if (val.type == Value::FUN) callback(val.data.fun);
        }
    }

    void Object::set_val(const fstring &key, Value val) {
        str_map[key] = val;
    }

    void Scope::get_refs(const std::function<void(Allocation * )> &callback) {
        callback(vars);
        callback(prev_scope);
    }

    Value Scope::get_var(const fstring &name) const {
        if (vars->contains(name)) return vars->get_val(name);
        if (prev_scope == nullptr) return {.type = Value::NUL};
        return prev_scope->get_var(name);
    }

    void Scope::set_var(const fstring &name, Value val) {
        set_var(name, val, *this);
    }

    void Scope::set_var(const fstring &name, Value val, Scope &first) {
        if (vars->contains(name)) vars->set_val(name, val);
        if (prev_scope) prev_scope->set_var(name, val, first);
        else first.vars->set_val(name, val);
    }

    stack_pos_t VM::Stack::size() const { return stack.size(); }

    VM::Stack::Stack(VM &vm) : vm(vm), stack(AllocatorWrapper<Value>(vm.config.allocator)) {}

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
        (*fun)(*this, new_frame);
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

    void VM::Stack::discard() {
        stack_pos_t sep_pos = find_sep(0);
        pop(sep_pos);
    }

    void VM::Stack::call_op(Frame *frame, Operator op) {
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

    stack_pos_t VM::Stack::abs(stack_pos_t pos) const { return pos < 0 ? size() + pos : pos; }

    void VM::Stack::push_fun(Function *fun) {
        push({.type=Value::FUN, .data = {.fun = fun}});
    }

    void VM::Stack::push_obj(Object *obj) {
        push({.type = Value::OBJ, .data = {.obj = obj}});
    }

    VM::VM(VM::Config config) : config(config), stacks(AllocatorWrapper<Stack *>(config.allocator)), mem(*this) {}

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

    void VM::Stack::exec_bytecode(Frame *frame, Scope *scope, Bytecode *bytecode_obj, size_t offset) {
        const char *bytecode_start = bytecode_obj->get_data();
        const char *bytecode = bytecode_start + offset;
        size_t ip = 0;
        while (true) {
            auto opcode = (Opcode) bytecode[ip];
            switch (opcode) {
                case Opcode::NOP:
                    ip++;
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
                    call_op(frame, oper);
                    break;
                }
                case Opcode::DIS: {
                    ip++;
                    discard();
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
                    size_t pos = 0;
                    memcpy(&pos, bytecode + ip, sizeof(size_t));
                    ip += sizeof(size_t);
                    push_fun(vm.mem.gc_new<CompiledFunction>(scope, bytecode_obj, pos));
                    break;
                }
                case Opcode::OBJ: {
                    ip++;
                    push_obj(scope->vars);
                    break;
                }
                case Opcode::GET: {
                    ip++;
                    size_t pos = 0;
                    memcpy(&pos, bytecode + ip, sizeof(size_t));
                    ip += sizeof(size_t);
                    fstring name(reinterpret_cast<const wchar_t *>(bytecode_start + pos), vm.mem.str_alloc());
                    push(scope->get_var(name));
                    break;
                }
                case Opcode::REV: {
                    ip++;
                    reverse();
                    break;
                }
                case Opcode::SET: {
                    ip++;
                    size_t pos = 0;
                    memcpy(&pos, bytecode + ip, sizeof(size_t));
                    ip += sizeof(size_t);
                    fstring name(reinterpret_cast<const wchar_t *>(bytecode_start + pos), vm.mem.str_alloc());
                    if (get(-1).type != Value::SEP) scope->set_var(name, pop());
                    break;
                }
                default:
                    throw std::runtime_error("unknown opcode");
            }
        }
    }

    void VM::Stack::reverse() {
        stack_pos_t i = find_sep() + 1, j = -1;
        while (i < j) std::swap(get(i++), get(j--));
    }

    void VM::MemoryManager::gc_track(VM::Allocation *alloc) {
        if (gc_tracked.contains(alloc)) throw std::runtime_error("allocation is already tracked");
        gc_tracked.insert(alloc);
        gc_pinned.insert(alloc);
    }

    VM::MemoryManager::~MemoryManager() {
        for (Allocation *alloc: gc_tracked) {
            (*alloc).~Allocation();
            free(alloc);
        }
    }

    void VM::MemoryManager::gc_cycle() {
        std::queue<Allocation *, std::deque<Allocation *, AllocatorWrapper<Allocation *>>>
                queue(std_alloc<Allocation *>());
        auto marked = gc_pinned;
        for (auto *a: gc_pinned) queue.push(a);
        while (!queue.empty()) {
            auto *alloc = queue.front();
            queue.pop();
            alloc->get_refs([&marked, &queue](Allocation *ref) {
                if (ref && !marked.contains(ref)) queue.push(ref), marked.insert(ref);
            });
        }
        for (auto iter = gc_tracked.begin(); iter != gc_tracked.end();) {
            if (marked.contains(*iter)) iter++;
            else {
                Allocation *alloc = *iter;
                alloc->~Allocation();
                free(alloc);
                iter = gc_tracked.erase(iter);
            }
        }
    }

    void Frame::get_refs(const std::function<void(Allocation * )> &callback) {
        callback(prev_frame);
    }

    void CompiledFunction::get_refs(const std::function<void(Allocation * )> &callback) {
        callback(bytecode);
        callback(scope);
    }

    const char *Bytecode::get_data() {
        return data;
    }

    Bytecode::~Bytecode() {
        allocator->free(data);
    }
}