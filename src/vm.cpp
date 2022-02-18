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
        return str_map.contains(key) ? str_map.at(key) : Value{.type=Value::NUL};
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

    void VM::Stack::pop(stack_pos_t pos) {
        if (pos < 0) pos += size();
        stack.resize(pos);
    }

    void VM::Stack::call(Function *fun, Frame *frame) {
        auto *new_frame = vm.mem.gc_new<Frame>(frame);
        (*fun)(*this, new_frame);
        vm.mem.gc_unpin(new_frame);
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

    void VM::Stack::call_operator(Frame *frame, Operator op) {
        stack_pos_t pos_a = find_sep() + 1, pos_b = find_sep(pos_a - 1) + 1;
        size_t cnt_a = 0 - pos_a, cnt_b = pos_a - pos_b - 1;
        if (cnt_a == 0) { // unary operators
            if (cnt_b != 1) assert_failed("unary operator on multiple values"); // TODO
            switch (get(-2).type) {
                case Value::INT: {
                    int64_t result;
                    switch (op) {
                        case Operator::PLUS: {
                            result = +get(-2).data.num;
                            break;
                        }
                        case Operator::MINUS: {
                            result = -get(-2).data.num;
                            break;
                        }
                        default:
                            assert_failed("invalid unary operator for integer"); // TODO
                    }
                    pop(-3);
                    push_int(result);
                    break;
                }
                case Value::BLN: {
                    if (op != Operator::NOT) assert_failed("invalid unary operator for boolean"); // TODO
                    pop(-1);
                    bool result = !as_boolean();
                    pop(-2);
                    push_bln(result);
                    break;
                }
                default:
                    assert_failed("invalid operand for unary operator"); // TODO
            }
            return;
        }
        if (cnt_a != 1) assert_failed("first operand is multiple values");
        switch (get(pos_a).type) {
            case Value::NUL: {
                assert_failed("invalid operation on nul"); // TODO
                break;
            }
            case Value::INT: {
                FS_ASSERT(cnt_b == 1 && get(-3).type == Value::INT);
                int64_t left = get(-1).data.num, right = get(-3).data.num;
                switch (op) {
                    case Operator::TIMES: {
                        int64_t result = left * right;
                        pop(-4);
                        push_int(result);
                        break;
                    }
                    case Operator::DIVIDE: {
                        int64_t result = left / right;
                        pop(-4);
                        push_int(result);
                        break;
                    }
                    case Operator::PLUS: {
                        int64_t result = left + right;
                        pop(-4);
                        push_int(result);
                        break;
                    }
                    case Operator::MINUS: {
                        int64_t result = left - right;
                        pop(-4);
                        push_int(result);
                        break;
                    }
                    case Operator::MODULO: {
                        int64_t result = left % right;
                        pop(-4);
                        push_int(result);
                        break;
                    }
                    case Operator::GREATER: {
                        bool result = left > right;
                        pop(-4);
                        push_bln(result);
                        break;
                    }
                    case Operator::LESS: {
                        bool result = left < right;
                        pop(-4);
                        push_bln(result);
                        break;
                    }
                    case Operator::LESS_EQUAL: {
                        bool result = left <= right;
                        pop(-4);
                        push_bln(result);
                        break;
                    }
                    case Operator::GREATER_EQUAL: {
                        bool result = left >= right;
                        pop(-4);
                        push_bln(result);
                        break;
                    }
                    case Operator::DIFFERS: {
                        bool result = left != right;
                        pop(-4);
                        push_bln(result);
                        break;
                    }
                    case Operator::EQUALS: {
                        bool result = left == right;
                        pop(-4);
                        push_bln(result);
                        break;
                    }
                    default:
                        assert_failed("invalid operation on integer"); // TODO
                }
                break;
            }
            case Value::OBJ: {
                FS_ASSERT(cnt_b == 1 && get(-3).type == Value::OBJ);
                Object *left = get(-1).data.obj, *right = get(-3).data.obj;
                bool result;
                switch (op) {
                    case Operator::EQUALS:
                        result = left == right;
                        break;
                    case Operator::DIFFERS:
                        result = left != right;
                        break;
                    default:
                        assert_failed("invalid operation on object"); // TODO
                }
                pop(-4);
                push_bln(result);
                break;
            }
            case Value::FUN:
                switch (op) {
                    case Operator::CALL: {
                        Function *fun = get(-1).data.fun;
                        vm.mem.gc_pin(fun);
                        pop(-2);
                        call(fun, frame);
                        vm.mem.gc_unpin(fun);
                        break;
                    }
                    default:
                        assert_failed("invalid operation on function"); // TODO

                }
                break;
            case Value::BLN: {
                FS_ASSERT(cnt_b == 1 && get(-3).type == Value::BLN); // TODO
                bool left = get(-1).data.bln, right = get(-3).data.bln;
                bool result;
                switch (op) {
                    case Operator::EQUALS:
                        result = left == right;
                        break;
                    case Operator::DIFFERS:
                        result = left != right;
                        break;
                    default:
                        assert_failed("invalid operation on boolean"); // TODO
                }
                pop(-4);
                push_bln(result);
                break;
            }
            case Value::ARR: {
                FS_ASSERT(cnt_b == 1 && get(-3).type == Value::ARR); // TODO
                FS_ASSERT(op == Operator::CALL); // TODO
                Array *arr = get(-1).data.arr, *ind = get(-3).data.arr;
                pop(-4);
                for (size_t pos = 0; pos < ind->len; pos++) {
                    FS_ASSERT(ind->data[pos].type == Value::INT); // TODO
                    auto i = ind->data[pos].data.num;
                    FS_ASSERT(i >= 0 && i < arr->len); // TODO
                    push(arr->data[i]);
                }
                break;
            }
            default:
                assert_failed("unknown value type"); // TODO
        }
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
        stacks.push_back(mem.gc_new<Stack>(*this));
        return stacks.size() - 1;
    }

    VM::~VM() = default;

    void VM::Stack::exec_bytecode(Frame *frame, Scope *scope, Bytecode *bytecode, size_t offset) {
        const auto *size_const = reinterpret_cast<const size_t *>(bytecode->get_data());
        const auto *int_const = reinterpret_cast<const int64_t *>(bytecode->get_data());
        const auto *ip = reinterpret_cast<const Instruction *>(bytecode->get_data() + offset);
        while (true) {
            Instruction inst = *ip;
            switch (inst.op) {
                case Opcode::NOP:
                    break;
                    ip++;
                case Opcode::NUL: {
                    push_nul();
                    ip++;
                    break;
                }
                case Opcode::SEP: {
                    push_sep();
                    ip++;
                    break;
                }
                case Opcode::INT: {
                    push_int(int_const[inst.u16]);
                    ip++;
                    break;
                }
                case Opcode::OP: {
                    auto oper = (Operator) inst.u8;
                    call_operator(frame, oper);
                    ip++;
                    break;
                }
                case Opcode::DIS: {
                    discard();
                    ip++;
                    break;
                }
                case Opcode::NS: {
                    auto *vars = vm.mem.gc_new<Object>(vm);
                    scope = vm.mem.gc_new<Scope>(vars, scope);
                    vm.mem.gc_unpin(vars);
                    ip++;
                    break;
                }
                case Opcode::DS: {
                    Scope *prev = scope->prev_scope;
                    vm.mem.gc_unpin(scope);
                    scope = prev;
                    ip++;
                    break;
                }
                case Opcode::END:
                    return;
                case Opcode::FUN: {
                    Function *fun = vm.mem.gc_new<CompiledFunction>(scope, bytecode, size_const[inst.u16]);
                    push_fun(fun);
                    vm.mem.gc_unpin(fun);
                    ip++;
                    break;
                }
                case Opcode::OBJ: {
                    push_obj(scope->vars);
                    ip++;
                    break;
                }
                case Opcode::VGT: {
                    fstring name(reinterpret_cast<const wchar_t *>(bytecode->get_data() + size_const[inst.u16]),
                                 vm.mem.str_alloc());
                    push(scope->get_var(name));
                    ip++;
                    break;
                }
                case Opcode::VST: {
                    fstring name(reinterpret_cast<const wchar_t *>(bytecode->get_data() + size_const[inst.u16]),
                                 vm.mem.str_alloc());
                    if (get(-1).type != Value::SEP) {
                        scope->set_var(name, get(-1));
                        pop();
                    }
                    ip++;
                    break;
                }
                case Opcode::GET: {
                    fstring name(reinterpret_cast<const wchar_t *>(bytecode->get_data() + size_const[inst.u16]),
                                 vm.mem.str_alloc());
                    FS_ASSERT(get(-1).type == Value::OBJ); // TODO
                    FS_ASSERT(get(-2).type == Value::SEP); // TODO
                    Object *obj = get(-1).data.obj;
                    vm.mem.gc_pin(obj);
                    pop();
                    pop();
                    push(obj->get_val(name));
                    vm.mem.gc_unpin(obj);
                    ip++;
                    break;
                }
                case Opcode::SET: {
                    fstring name(reinterpret_cast<const wchar_t *>(bytecode->get_data() + size_const[inst.u16]),
                                 vm.mem.str_alloc());
                    FS_ASSERT(get(-1).type == Value::OBJ); // TODO
                    FS_ASSERT(get(-2).type == Value::SEP); // TODO
                    Object *obj = get(-1).data.obj;
                    vm.mem.gc_pin(obj);
                    pop();
                    pop();
                    if (get(-1).type != Value::SEP) {
                        obj->set_val(name, get(-1));
                        pop();
                    }
                    vm.mem.gc_unpin(obj);
                    ip++;
                    break;
                }
                case Opcode::PBY: {
                    push_bln(true);
                    ip++;
                    break;
                }
                case Opcode::PBN: {
                    push_bln(false);
                    ip++;
                    break;
                }
                case Opcode::JMP: {
                    ip = reinterpret_cast<const Instruction *>(bytecode->get_data() + size_const[inst.u16]);
                    break;
                }
                case Opcode::JN: {
                    if (!as_boolean()) {
                        ip = reinterpret_cast<const Instruction *>(bytecode->get_data() + size_const[inst.u16]);
                    } else ip++;
                    break;
                }
                case Opcode::POP: {
                    pop();
                    ip++;
                    break;
                }
                case Opcode::ARR: {
                    push_arr();
                    ip++;
                    break;
                }
                default:
                    throw std::runtime_error("unknown opcode");
            }
        }
    }

    void VM::Stack::get_refs(const std::function<void(Allocation *)> &callback) {
        for (const Value &val: stack) {
            if (val.type == Value::OBJ) callback(val.data.obj);
            if (val.type == Value::FUN) callback(val.data.fun);
        }
    }

    void VM::Stack::push_bln(bool bln) {
        push({.type = Value::BLN, .data={.bln=bln}});
    }

    bool VM::Stack::as_boolean() {
        if (get(-1).type != Value::BLN) assert_failed("no implicit conversion to boolean");
        return get(-1).data.bln;
    }

    void VM::Stack::push_arr() {
        auto pos = find_sep();
        auto len = -pos - 1;
        auto *array = vm.mem.gc_new<Array>(vm, len);
        memcpy(array->data, stack.data() + abs(pos) + 1, len * sizeof(Value));
        pop(pos);
        push({Value::ARR, {.arr = array}});
    }

    void VM::MemoryManager::gc_track(VM::Allocation *alloc) {
        if (gc_tracked.contains(alloc)) throw AssertionError("allocation is already tracked");
        gc_tracked.insert(alloc);
        gc_pins[alloc] = 1;
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
        fset<Allocation *> marked(std_alloc<Allocation *>());
        fset<Allocation *> unpinned(std_alloc<Allocation *>());
        for (const auto&[alloc, cnt]: gc_pins) {
            if (cnt == 0) {
                unpinned.insert(alloc);
                continue;
            }
            marked.insert(alloc);
            queue.push(alloc);
        }
        for (auto *alloc: unpinned) gc_pins.erase(alloc);
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

    void VM::MemoryManager::gc_pin(VM::Allocation *alloc) {
        if (!gc_tracked.contains(alloc)) [[unlikely]] throw AssertionError("allocation is not tracked");
        gc_pins[alloc]++;
    }

    void VM::MemoryManager::gc_unpin(VM::Allocation *alloc) {
        if (!gc_tracked.contains(alloc)) [[unlikely]] throw AssertionError("allocation is not tracked");
        if (!gc_pins[alloc]) [[unlikely]] throw AssertionError("unpin mismatch");
        gc_pins[alloc]--;
        if (gc_pins[alloc] == 0) gc_pins.erase(alloc);
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

    void Array::get_refs(const std::function<void(Allocation * )> &callback) {
        for (size_t pos = 0; pos < len; pos++) {
            const Value &val = data[pos];
            if (val.type == Value::OBJ) callback(val.data.obj);
            if (val.type == Value::FUN) callback(val.data.fun);
        }
    }

    Array::~Array() { vm.mem.free(data); }
}