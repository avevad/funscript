//
// Created by avevad on 11/20/21.
//

#include "vm.h"
#include "common.h"

#include <stdexcept>
#include <cstring>

namespace funscript {

    bool Table::contains(const std::wstring &key) {
        return str_map.contains(key);
    }

    Value &Table::var(const std::wstring &key) {
        return str_map[key];
    }

    Value &Scope::resolve(const std::wstring &key) {
        if (!contains(key)) return vars->var(key);
        if (!vars->contains(key) && prev_scope != nullptr) return prev_scope->resolve(key);
        return vars->var(key);
    }

    bool Scope::contains(const std::wstring &key) const {
        if (vars->contains(key)) return true;
        return prev_scope != nullptr && prev_scope->contains(key);
    }

    stack_pos_t VM::Stack::length() const { return len; }

    VM::Stack::Stack(VM &vm) : vm(vm), size(vm.config.stack_size), stack(new Value[size]) {}

    const Value &VM::Stack::operator[](stack_pos_t pos) { return get(pos); }

    void VM::Stack::push_sep() { push({Value::SEP}); }

    void VM::Stack::push_nul() { push({Value::NUL}); }

    void VM::Stack::push_int(int64_t num) { push({Value::INT, {.num = num}}); }

    void VM::Stack::push_tab() {
        push({Value::TAB, {.tab = new Table(vm)}});
    }

    void VM::Stack::add() { op(Operator::PLUS); }

    Value VM::Stack::pop() { return stack[--len]; }

    void VM::Stack::pop(stack_pos_t pos) {
        if (pos < 0) pos += len;
        len = pos;
    }

    VM::Stack::~Stack() { delete[] stack; }

    void VM::Stack::push(const Value &e) { stack[len++] = e; }

    Value &VM::Stack::get(stack_pos_t pos) {
        if (pos < 0) pos += len;
        return stack[pos];
    }

    stack_pos_t VM::Stack::find_sep(stack_pos_t before) {
        stack_pos_t pos = before - 1;
        while (get(pos).type != Value::SEP) pos--;
        return pos;
    }

    void VM::Stack::mov() {
        stack_pos_t ref_sep = abs(find_sep(0)), val_sep = abs(find_sep(ref_sep));
        stack_pos_t ref_pos = ref_sep + 1, val_pos = val_sep + 1;
        while (get(val_pos).type != Value::SEP && ref_pos != len) *get(ref_pos++).data.ref = get(val_pos++);
        pop(ref_sep);
        memmove(stack + val_sep, stack + val_sep + 1, sizeof(Value) * (len - val_sep - 1));
        len--;
    }

    void VM::Stack::dis() {
        stack_pos_t sep_pos = find_sep(0);
        pop(sep_pos);
    }

    void VM::Stack::op(Operator op) {
        stack_pos_t pos_b = find_sep() + 1, pos_a = find_sep(pos_b - 1) + 1;
        size_t cnt_b = 0 - pos_b, cnt_a = pos_b - pos_a - 1;
        if (cnt_a == 0) {
            if (cnt_b != 1) throw std::runtime_error(""); // TODO
            if (get(pos_b).type == Value::INT) {
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
                        throw std::runtime_error(""); // TODO
                }
                pop(-3);
                push_int(result);
            } else throw std::runtime_error(""); // TODO
        } else if (cnt_a == 1) {
            if (get(pos_a).type == Value::INT) {
                if (cnt_b != 1 || get(pos_b).type != Value::INT) throw std::runtime_error(""); // TODO
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
                    default:
                        throw std::runtime_error(""); // TODO
                }
                pop(-4);
                push_int(result);
            } else throw std::runtime_error(""); // TODO
        } else throw std::runtime_error(""); // TODO

    }

    void VM::Stack::push_ref(Scope *scope, const std::wstring &key) {
        push({.type = Value::REF, .data = {.ref = &scope->resolve(key)}});
    }

    void VM::Stack::push_val(Scope *scope, const std::wstring &key) {
        push(scope->resolve(key));
    }

    stack_pos_t VM::Stack::abs(stack_pos_t pos) const { return pos < 0 ? len + pos : pos; }

    void VM::Stack::push_fun(fun_def def, const void *data, Scope *scope) {
        auto *fun = new Function{.def = def, .data = data, .scope = scope};
        push({.type=Value::FUN, .data = {.fun = fun}});
    }

    VM::VM(VM::Config config) : config(config) {}

    VM::Stack &VM::stack(size_t id) { return stacks[id]; }

    size_t VM::new_stack() {
        stacks.emplace_back(*this);
        return stacks.size() - 1;
    }

    void exec_bytecode(VM::Stack *stack_ptr, Frame *frame, const void *data, Scope *scope) {
        VM::Stack &stack = *stack_ptr;
        const char *bytecode = reinterpret_cast<const char *>(data);
        size_t ip = 0;
        while (true) {
            auto opcode = (Opcode) bytecode[ip];
            switch (opcode) {
                case Opcode::NOP:
                    break;
                case Opcode::NUL: {
                    ip++;
                    stack.push_nul();
                    break;
                }
                case Opcode::SEP: {
                    ip++;
                    stack.push_sep();
                    break;
                }
                case Opcode::INT: {
                    ip++;
                    int64_t num;
                    memcpy(&num, bytecode + ip, sizeof(int64_t));
                    ip += sizeof(int64_t);
                    stack.push_int(num);
                    break;
                }
                case Opcode::OP: {
                    ip++;
                    auto op = (Operator) bytecode[ip];
                    ip++;
                    stack.op(op);
                    break;
                }
                case Opcode::REF: {
                    ip++;
                    size_t pos = 0;
                    memcpy(&pos, bytecode + ip, sizeof(size_t));
                    ip += sizeof(size_t);
                    std::wstring key(reinterpret_cast<const wchar_t *>(bytecode + pos));
                    stack.push_ref(scope, key);
                    break;
                }
                case Opcode::VAL: {
                    ip++;
                    size_t pos = 0;
                    memcpy(&pos, bytecode + ip, sizeof(size_t));
                    ip += sizeof(size_t);
                    std::wstring key(reinterpret_cast<const wchar_t *>(bytecode + pos));
                    stack.push_val(scope, key);
                    break;
                }
                case Opcode::MOV: {
                    ip++;
                    stack.mov();
                    break;
                }
                case Opcode::DIS: {
                    ip++;
                    stack.dis();
                    break;
                }
                case Opcode::NS: {
                    ip++;
                    stack.push_tab();
                    scope = new Scope(stack.pop().data.tab, scope);
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
                    stack.push_fun(exec_bytecode, bytecode + pos, scope);
                    break;
                }
                default:
                    throw std::runtime_error("unknown opcode"); // TODO
            }
        }
    }
}