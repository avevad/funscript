#include "vm.hpp"

#include <queue>
#include <utility>
#include <sstream>
#include <cstring>

namespace funscript {

    VM::VM(VM::Config config) : config(config), mem(config.mm),
                                modules(mem.std_alloc<decltype(modules)::value_type>()) {}

    void VM::register_module(const funscript::FStr &name, funscript::VM::Module *mod) {
        modules.insert({name, MemoryManager::AutoPtr(mod)});
    }

    std::optional<VM::Module *> VM::get_module(const funscript::FStr &name) {
        if (!modules.contains(name)) return std::nullopt;
        return modules.at(name).get();
    }

    void VM::Stack::get_refs(const std::function<void(Allocation *)> &callback) {
        for (const auto &val : values) val.get_ref(callback);
        callback(cur_frame);
    }

    VM::Stack::Stack(VM &vm, Function *start) : Allocation(vm), values(vm.mem.std_alloc<Value>()),
                                                cur_frame(vm.mem.gc_new_auto<Frame>(start).get()) {}

    VM::Stack::Stack(funscript::VM &vm) : Allocation(vm), values(vm.mem.std_alloc<Value>()),
                                          cur_frame(nullptr) {}

    VM::Stack::pos_t VM::Stack::size() const {
        return values.size(); // NOLINT(cppcoreguidelines-narrowing-conversions)
    }

    void VM::Stack::push_nul() { return push({Type::NUL}); }

    void VM::Stack::push_int(fint num) { return push({Type::INT, {.num = num}}); }

    void VM::Stack::push_flp(fflp flp) { return push({Type::FLP, {.flp = flp}}); }

    void VM::Stack::push_obj(Object *obj) { return push({Type::OBJ, {.obj = obj}}); }

    void VM::Stack::push_fun(Function *fun) { return push({Type::FUN, {.fun = fun}}); }

    void VM::Stack::push_bln(fbln bln) { return push({Type::BLN, {.bln = bln}}); }

    void VM::Stack::push_str(String *str) { return push({Type::STR, {.str = str}}); }

    void VM::Stack::push_arr(VM::Array *arr) { return push({Type::ARR, {.arr = arr}}); }

    void VM::Stack::push_ptr(Allocation *ptr) { return push({Type::PTR, {.ptr = ptr}}); }

    void VM::Stack::as_boolean() {
        if (find_beg() + 1 != size() || top_value().type != Type::BLN) {
            panic("no implicit conversion to boolean");
        }
        bool bln = top_value().data.bln;
        pop_pack();
        push_bln(bln);
    }

    bool VM::Stack::discard() {
        bool res = seps == 0;
        pop_pack();
        return res;
    }

    void VM::Stack::push(const Value &e) {
        if (values.size() >= vm.config.stack_values_max) throw StackOverflowError();
        values.push_back(e);
        values.back().seps = seps;
        seps = 0;
    }

    void VM::Stack::separate() {
        seps++;
    }

    void VM::Stack::pop_pack() {
        if (seps) seps--;
        else {
            pos_t beg = find_beg();
            seps = values[beg].seps - 1;
            values.resize(beg);
        }
    }

    VM::Stack::pos_t VM::Stack::find_beg() const {
        if (seps) return size();
        if (!size()) assertion_failed("no value packs left");
        for (pos_t pos = size() - 1;; pos--) {
            if (values[pos].seps) return pos;
            if (!pos) assertion_failed("no value packs left");
        }
    }

    void VM::Stack::pop_value() {
        if (seps || !size()) assertion_failed("empty value pack");
        seps = values.back().seps;
        values.pop_back();
    }

    const VM::Value &VM::Stack::top_value() const {
        if (seps || !size()) assertion_failed("empty value pack");
        return values.back();
    }

    size_t VM::Stack::pack_size() const {
        return size() - find_beg();
    }

    const VM::Value *VM::Stack::raw_values() const {
        return values.data();
    }

    size_t VM::Stack::get_seps_count() const {
        return seps;
    }

    VM::Stack::pos_t VM::Stack::find_beg(VM::Stack::pos_t before) const {
        if (!before) assertion_failed("no value packs left");
        for (pos_t pos = before - 1;; pos--) {
            if (values[pos].seps) return pos;
            if (!pos) assertion_failed("no value packs left");
        }
    }

    std::pair<VM::Stack::pos_t, VM::Stack::pos_t> VM::Stack::find_operands() const {
        if (seps >= 2) return {size(), size()};
        if (seps == 1) return {size(), find_beg(size())};
        pos_t pos_a = find_beg();
        if (values[pos_a].seps > 1) return {pos_a, pos_a};
        return {pos_a, find_beg(pos_a)};
    }

    volatile sig_atomic_t VM::Stack::kbd_int = 0;

    void VM::Stack::exec_bytecode(Module *mod, Scope *scope, Bytecode *bytecode_obj, size_t offset,
                                  pos_t frame_start, size_t frame_seps) {
        const auto *bytecode = bytecode_obj->bytes.data();
        const auto *ip = reinterpret_cast<const Instruction *>(bytecode + offset);
        auto cur_scope = MemoryManager::AutoPtr(scope);
        const char *meta_chunk = nullptr;
        code_met_t meta{.filename = nullptr};
        try {
            while (true) {
                if (kbd_int) {
                    kbd_int = 0;
                    panic("keyboard interrupt");
                }
                Instruction ins = *ip;
                if (meta_chunk && ins.meta) {
                    meta.position = *reinterpret_cast<const code_pos_t *>(meta_chunk + ins.meta);
                }
                switch (ins.op) {
                    case Opcode::NOP:
                        ip++;
                        break;
                    case Opcode::MET: {
                        meta.filename = meta_chunk = bytecode + ins.u64;
                        cur_frame->meta_ptr = &meta;
                        ip++;
                        break;
                    }
                    case Opcode::VAL: {
                        Value val(static_cast<Type>(ins.u16), {.num = static_cast<fint>(ins.u64)});
                        if (val.type == Type::FUN) {
                            auto *fun = vm.mem.gc_new<BytecodeFunction>(vm, mod, cur_scope.get(), bytecode_obj,
                                                                        size_t(ins.u64));
                            val.data.fun = fun;
                        }
                        try {
                            push(val);
                        } catch (const StackOverflowError &e) {
                            if (val.type == Type::FUN) vm.mem.gc_unpin(val.data.fun);
                            throw;
                        }
                        if (val.type == Type::FUN) vm.mem.gc_unpin(val.data.fun);
                        ip++;
                        break;
                    }
                    case Opcode::OBJ: {
                        cur_scope->vars->init_values(values.data() + find_beg(), values.data() + size());
                        pop_pack();
                        push_obj(cur_scope->vars);
                        ip++;
                        break;
                    }
                    case Opcode::SEP: {
                        separate();
                        ip++;
                        break;
                    }
                    case Opcode::GET: {
                        FStr name(reinterpret_cast<const FStr::value_type *>(bytecode + ins.u64), vm.mem.str_alloc());
                        if (seps != 0) {
                            pop_pack();
                            auto var = cur_scope->vars->get_field(name);
                            if (!var.has_value()) panic("no such field: '" + std::string(name) + "'");
                            push(var.value());
                        } else {
                            if (find_beg() + 1 < size()) panic("multiple values cannot be indexed");
                            if (top_value().type != Type::OBJ) panic("only objects are able to be indexed");
                            auto obj = MemoryManager::AutoPtr(top_value().data.obj);
                            pop_pack();
                            auto field = obj->get_field(name);
                            if (!field.has_value()) {
                                panic("no such field: '" + std::string(name) + "'");
                            }
                            push(field.value());
                        }
                        ip++;
                        break;
                    }
                    case Opcode::SET: {
                        FStr name(reinterpret_cast<const FStr::value_type *>(bytecode + ins.u64), vm.mem.str_alloc());
                        if (seps != 0) {
                            pop_pack();
                            if (seps) panic("not enough values to assign");
                            if (top_value().type == Type::FUN && !top_value().data.fun->get_name().has_value()) {
                                top_value().data.fun->assign_name(name);
                            }
                            cur_scope->vars->set_field(name, top_value());
                            pop_value();
                        } else {
                            if (find_beg() + 1 < size()) panic("multiple values cannot be indexed");
                            if (top_value().type != Type::OBJ) panic("only objects are able to be indexed");
                            auto obj = MemoryManager::AutoPtr(top_value().data.obj);
                            pop_pack();
                            if (seps) return panic("not enough values to assign");
                            if (top_value().type == Type::FUN && !top_value().data.fun->get_name().has_value()) {
                                top_value().data.fun->assign_name(name);
                            }
                            obj->set_field(name, top_value());
                            pop_value();
                        }
                        ip++;
                        break;
                    }
                    case Opcode::VGT: {
                        FStr name(reinterpret_cast<const FStr::value_type *>(bytecode + ins.u64), vm.mem.str_alloc());
                        auto var = cur_scope->get_var(name);
                        if (!var.has_value()) {
                            panic("no such variable: '" + std::string(name) + "'");
                        }
                        push(var.value());
                        ip++;
                        break;
                    }
                    case Opcode::VST: {
                        FStr name(reinterpret_cast<const FStr::value_type *>(bytecode + ins.u64), vm.mem.str_alloc());
                        if (seps != 0) return panic("not enough values to assign");
                        if (!cur_scope->set_var(name, top_value())) {
                            panic("no such variable: '" + std::string(name) + "'");
                        }
                        if (top_value().type == Type::FUN && !top_value().data.fun->get_name().has_value()) {
                            top_value().data.fun->assign_name(name);
                        }
                        pop_value();
                        ip++;
                        break;
                    }
                    case Opcode::SCP: {
                        if (ins.u16) {
                            auto vars = vm.mem.gc_new_auto<Object>(vm);
                            cur_scope = vm.mem.gc_new_auto<Scope>(vars.get(), cur_scope.get());
                        } else {
                            cur_scope.set(cur_scope->prev_scope);
                        }
                        ip++;
                        break;
                    }
                    case Opcode::DIS: {
                        if (discard() && ins.u16) panic("too many values");
                        ip++;
                        break;
                    }
                    case Opcode::REV: {
                        reverse();
                        ip++;
                        break;
                    }
                    case Opcode::OPR: {
                        auto op = static_cast<Operator>(ins.u16);
                        call_operator(op);
                        ip++;
                        break;
                    }
                    case Opcode::END: {
                        return;
                    }
                    case Opcode::JNO: {
                        as_boolean();
                        if (!top_value().data.bln) ip = reinterpret_cast<const Instruction *>(bytecode + ins.u64);
                        else ip++;
                        pop_value();
                        break;
                    }
                    case Opcode::JYS: {
                        as_boolean();
                        if (top_value().data.bln) ip = reinterpret_cast<const Instruction *>(bytecode + ins.u64);
                        else ip++;
                        pop_value();
                        break;
                    }
                    case Opcode::JMP: {
                        ip = reinterpret_cast<const Instruction *>(bytecode + ins.u64);
                        break;
                    }
                    case Opcode::STR: {
                        FStr str(reinterpret_cast<const FStr::value_type *>(bytecode + ins.u64),
                                 reinterpret_cast<const FStr::value_type *>(bytecode + ins.u64 + ins.u16),
                                 vm.mem.str_alloc());
                        auto str_obj = vm.mem.gc_new_auto<String>(vm, str);
                        push_str(str_obj.get());
                        ip++;
                        break;
                    }
                    case Opcode::ARR: {
                        pos_t beg = find_beg();
                        auto arr = vm.mem.gc_new_auto<Array>(vm, values.data() + beg, size() - beg);
                        pop_pack();
                        ip++;
                        push_arr(arr.get());
                        break;
                    }
                    case Opcode::MOV: {
                        call_assignment();
                        ip++;
                        break;
                    }
                    case Opcode::DUP: {
                        duplicate();
                        ip++;
                        break;
                    }
                    case Opcode::REM: {
                        join();
                        ip++;
                        break;
                    }
                    case Opcode::INS: {
                        if (seps) seps++;
                        else values.back().seps++;
                        ip++;
                        break;
                    }
                    case Opcode::DP1: {
                        duplicate_value();
                        ip++;
                        break;
                    }
                    case Opcode::CHK: {
                        call_type_check();
                        ip++;
                        break;
                    }
                    case Opcode::EXT: {
                        if (find_beg() + 1 != size()) panic("single result object expected");
                        auto obj = MemoryManager::AutoPtr(top_value().data.obj);
                        pop_pack();
                        bool is_err = obj->contains_field(ERR_FLAG_NAME);
                        if (ins.u64) {
                            if (is_err) [[unlikely]] {
                                ip++;
                            } else {
                                size_t push_cnt = obj->get_values().size();
                                values.reserve(size() + push_cnt);
                                for (size_t pos = 0; pos < push_cnt; pos++) push(obj->get_values()[pos]);
                                ip = reinterpret_cast<const Instruction *>(bytecode + ins.u64);
                            }
                        } else {
                            if (is_err) [[unlikely]] {
                                values.resize(frame_start);
                                seps = frame_seps;
                                push_obj(obj.get());
                                return;
                            } else {
                                size_t push_cnt = obj->get_values().size();
                                values.reserve(size() + push_cnt);
                                for (size_t pos = 0; pos < push_cnt; pos++) push(obj->get_values()[pos]);
                                ip++;
                            }
                        }
                        break;
                    }
                }
            }
        } catch (const OutOfMemoryError &e) {
            values.resize(frame_start);
            seps = frame_seps;
            scope = nullptr;
            panic("out of memory");
        } catch (const StackOverflowError &e) {
            values.resize(frame_start);
            seps = frame_seps;
            panic("stack overflow");
        }
    }

    void VM::Stack::exec_bytecode(VM::Module *mod, VM::Scope *scope, VM::Bytecode *bytecode_obj, size_t offset) {
        pos_t frame_pos = find_beg();
        size_t frame_seps = frame_pos == size() ? seps - 1 : values[frame_pos].seps - 1;
        exec_bytecode(mod, scope, bytecode_obj, offset, frame_pos, frame_seps);
    }

    void VM::Stack::call_operator(Operator op) {
        // Calculate stack positions of operands and their lengths
        auto [pos_a, pos_b] = find_operands();
        pos_t cnt_a = size() - pos_a, cnt_b = pos_a - pos_b;
        if (op == Operator::IS) {
            fbln res = false;
            if (cnt_a == cnt_b) {
                res = true;
                for (pos_t pos = pos_b; pos < pos_a; pos++) {
                    if (values[pos].type != values[pos + cnt_b].type) res = false;
                    if (std::memcmp(&values[pos].data, &values[pos + cnt_b].data, sizeof(values[pos].data)) != 0) {
                        res = false;
                    }
                }
            }
            pop_pack();
            pop_pack();
            push_bln(res);
            return;
        }
        if (cnt_a == 1 && top_value().type == Type::OBJ) {

            op_panic(op);
        }
        if (cnt_a == 1 && op == Operator::CALL && top_value().type == Type::FUN) {
            MemoryManager::AutoPtr<Function> fn(top_value().data.fun);
            pop_pack();
            call_function(fn.get());
            return;
        }
        if (cnt_a == 0 && cnt_b == 1) {
            pop_pack();
            ValueHolder val(top_value());
            pop_pack();
            switch (op) {
                case Operator::MINUS: {
                    if (val.get().type == Type::INT) {
                        push_int(-val.get().data.num);
                        break;
                    }
                    if (val.get().type == Type::FLP) {
                        push_flp(-val.get().data.flp);
                        break;
                    }
                    op_panic(op);
                }
                case Operator::NOT: {
                    if (val.get().type == Type::BLN) {
                        push_bln(!val.get().data.bln);
                        break;
                    }
                    op_panic(op);
                }
                default:
                    op_panic(op);
            }
            return;
        }
        if (cnt_a == 1 && cnt_b == 1) {
            ValueHolder val_a(top_value());
            pop_pack();
            ValueHolder val_b(top_value());
            pop_pack();
            switch (op) {
                case Operator::TIMES: {
                    if (val_a.get().type == Type::INT && val_b.get().type == Type::INT) {
                        push_int(val_a.get().data.num * val_b.get().data.num);
                        break;
                    }
                    if (val_a.get().type == Type::FLP && val_b.get().type == Type::FLP) {
                        push_flp(val_a.get().data.flp * val_b.get().data.flp);
                        break;
                    }
                    if (val_a.get().type == Type::ARR && val_b.get().type == Type::INT) {
                        fint k = val_b.get().data.num;
                        size_t len = val_a.get().data.arr->len();
                        Value *src = val_a.get().data.arr->begin();
                        auto dst = vm.mem.gc_new_auto<Array>(vm, len * k);
                        for (size_t pos = 0; pos < len * k; pos += len) {
                            std::copy(src, src + len, dst->begin() + pos);
                        }
                        push_arr(dst.get());
                        break;
                    }
                    if (val_a.get().type == Type::INT && val_b.get().type == Type::ARR) {
                        fint k = val_a.get().data.num;
                        size_t len = val_b.get().data.arr->len();
                        Value *src = val_b.get().data.arr->begin();
                        auto dst = vm.mem.gc_new_auto<Array>(vm, len * k);
                        for (size_t pos = 0; pos < len * k; pos += len) {
                            std::copy(src, src + len, dst->begin() + pos);
                        }
                        push_arr(dst.get());
                        break;
                    }
                    op_panic(op);
                }
                case Operator::DIVIDE: {
                    if (val_a.get().type == Type::INT && val_b.get().type == Type::INT) {
                        if (val_b.get().data.num == 0) panic("integer division by zero");
                        push_int(val_a.get().data.num / val_b.get().data.num);
                        break;
                    }
                    if (val_a.get().type == Type::FLP && val_b.get().type == Type::FLP) {
                        push_flp(val_a.get().data.flp / val_b.get().data.flp);
                        break;
                    }
                    op_panic(op);
                }
                case Operator::PLUS: {
                    if (val_a.get().type == Type::INT && val_b.get().type == Type::INT) {
                        push_int(val_a.get().data.num + val_b.get().data.num);
                        break;
                    }
                    if (val_a.get().type == Type::FLP && val_b.get().type == Type::FLP) {
                        push_flp(val_a.get().data.flp + val_b.get().data.flp);
                        break;
                    }
                    if (val_a.get().type == Type::STR && val_b.get().type == Type::STR) {
                        push_str(vm.mem.gc_new_auto<String>(vm,
                                                            val_a.get().data.str->bytes +
                                                            val_b.get().data.str->bytes).get());
                        break;
                    }
                    if (val_a.get().type == Type::ARR && val_b.get().type == Type::ARR) {
                        size_t a_len = val_a.get().data.arr->len();
                        Value *a_dat = val_a.get().data.arr->begin();
                        size_t b_len = val_b.get().data.arr->len();
                        Value *b_dat = val_b.get().data.arr->begin();
                        auto arr = vm.mem.gc_new_auto<Array>(vm, a_len + b_len);
                        std::copy(a_dat, a_dat + a_len, arr->begin());
                        std::copy(b_dat, b_dat + b_len, arr->begin() + a_len);
                        push_arr(arr.get());
                        break;
                    }
                    op_panic(op);
                }
                case Operator::MINUS: {
                    if (val_a.get().type == Type::INT && val_b.get().type == Type::INT) {
                        push_int(val_a.get().data.num - val_b.get().data.num);
                        break;
                    }
                    if (val_a.get().type == Type::FLP && val_b.get().type == Type::FLP) {
                        push_flp(val_a.get().data.flp - val_b.get().data.flp);
                        break;
                    }
                    op_panic(op);
                }
                case Operator::CALL: {
                    if (val_a.get().type == Type::ARR && val_b.get().type == Type::ARR) {
                        Array &arr = *val_a.get().data.arr;
                        Array &ind = *val_b.get().data.arr;
                        for (const auto &val : ind) {
                            if (val.type != Type::INT || val.data.num < 0 || arr.len() <= val.data.num) {
                                panic("invalid array index");
                            }
                            push(arr[val.data.num]);
                        }
                        break;
                    }
                    op_panic(op);
                }
                case Operator::MODULO: {
                    if (val_a.get().type == Type::INT && val_b.get().type == Type::INT) {
                        if (val_b.get().data.num == 0) panic("integer division by zero");
                        push_int(val_a.get().data.num % val_b.get().data.num);
                        break;
                    }
                    op_panic(op);
                }
                case Operator::EQUALS: {
                    if (val_a.get().type == Type::INT && val_b.get().type == Type::INT) {
                        push_bln(val_a.get().data.num == val_b.get().data.num);
                        break;
                    }
                    if (val_a.get().type == Type::FLP && val_b.get().type == Type::FLP) {
                        push_bln(val_a.get().data.flp == val_b.get().data.flp);
                        break;
                    }
                    op_panic(op);
                }
                case Operator::DIFFERS: {
                    if (val_a.get().type == Type::INT && val_b.get().type == Type::INT) {
                        push_bln(val_a.get().data.num != val_b.get().data.num);
                        break;
                    }
                    if (val_a.get().type == Type::FLP && val_b.get().type == Type::FLP) {
                        push_bln(val_a.get().data.flp != val_b.get().data.flp);
                        break;
                    }
                    op_panic(op);
                }
                case Operator::LESS: {
                    if (val_a.get().type == Type::INT && val_b.get().type == Type::INT) {
                        push_bln(val_a.get().data.num < val_b.get().data.num);
                        break;
                    }
                    if (val_a.get().type == Type::FLP && val_b.get().type == Type::FLP) {
                        push_bln(val_a.get().data.flp < val_b.get().data.flp);
                        break;
                    }
                    op_panic(op);
                }
                case Operator::GREATER: {
                    if (val_a.get().type == Type::INT && val_b.get().type == Type::INT) {
                        push_bln(val_a.get().data.num > val_b.get().data.num);
                        break;
                    }
                    if (val_a.get().type == Type::FLP && val_b.get().type == Type::FLP) {
                        push_bln(val_a.get().data.flp > val_b.get().data.flp);
                        break;
                    }
                    op_panic(op);
                }
                case Operator::LESS_EQUAL: {
                    if (val_a.get().type == Type::INT && val_b.get().type == Type::INT) {
                        push_bln(val_a.get().data.num <= val_b.get().data.num);
                        break;
                    }
                    if (val_a.get().type == Type::FLP && val_b.get().type == Type::FLP) {
                        push_bln(val_a.get().data.flp <= val_b.get().data.flp);
                        break;
                    }
                    op_panic(op);
                }
                case Operator::GREATER_EQUAL: {
                    if (val_a.get().type == Type::INT && val_b.get().type == Type::INT) {
                        push_bln(val_a.get().data.num >= val_b.get().data.num);
                        break;
                    }
                    if (val_a.get().type == Type::FLP && val_b.get().type == Type::FLP) {
                        push_bln(val_a.get().data.flp >= val_b.get().data.flp);
                        break;
                    }
                    op_panic(op);
                }
                default:
                    op_panic(op);
            }
            return;
        }
        op_panic(op);
    }

    void VM::Stack::call_assignment() {
        // Calculate stack positions of operands and their lengths
        auto [pos_a, pos_b] = find_operands();
        pos_t cnt_a = size() - pos_a, cnt_b = pos_a - pos_b;
        if (cnt_a == 1 && cnt_b == 1) {
            ValueHolder val_a(top_value());
            pop_pack();
            ValueHolder val_b(top_value());
            pop_pack();
            if (val_a.get().type == Type::ARR && val_b.get().type == Type::ARR) {
                Array &arr = *val_a.get().data.arr;
                Array &ind = *val_b.get().data.arr;
                for (const auto &val : ind) {
                    if (val.type != Type::INT || val.data.num < 0 || arr.len() <= val.data.num) {
                        panic("invalid array index");
                    }
                    if (seps) {
                        panic("not enough values");
                    }
                    arr[val.data.num] = top_value();
                    pop_value();
                }
                return;
            }
            return op_panic(Operator::CALL);
        }
        return op_panic(Operator::CALL);
    }

    void VM::Stack::call_type_check() {
        if (find_beg() + 1 != size() || top_value().type != Type::OBJ) panic("single type object expected");
        auto typ = MemoryManager::AutoPtr(top_value().data.obj);
        pop_pack();
        if (!typ->contains_field(TYPE_CHECK_NAME)) {
            panic("type object does not provide type check function");
        }
        auto fn = MemoryManager::AutoPtr(typ->get_field(TYPE_CHECK_NAME).value().data.fun);
        call_function(fn.get());
    }

    void VM::Stack::call_function(Function *fun) {
        if (cur_frame->depth + 1 >= vm.config.stack_frames_max) panic("stack overflow");
        cur_frame = vm.mem.gc_new_auto<Frame>(fun, cur_frame).get();
        fun->call(*this);
        cur_frame = cur_frame->prev_frame;
    }

    void VM::Stack::execute() {
        if (!cur_frame) assertion_failed("this execution stack is dead");
        try {
            cur_frame->fun->call(*this);
        } catch (Panic) {}
    }

    void VM::Stack::reverse() {
        if (seps) return;
        pos_t beg = find_beg();
        std::reverse(values.data() + find_beg(), values.data() + size());
        std::swap(values[beg].seps, values.back().seps);
    }

    void VM::Stack::duplicate() {
        if (seps) {
            seps++;
        } else {
            pos_t beg = find_beg();
            pos_t len = size() - beg;
            if (size() + len > vm.config.stack_values_max) throw StackOverflowError();
            values.resize(values.size() + len);
            std::copy(values.data() + beg, values.data() + beg + len, values.data() + beg + len);
            values[beg + len].seps = 1;
        }
    }

    void VM::Stack::join() {
        if (seps) seps--;
        else {
            pos_t pos = find_beg();
            values[pos].seps--;
        }
    }

    void VM::Stack::duplicate_value() {
        if (size() + 1 > vm.config.stack_values_max) throw StackOverflowError();
        values.push_back(values.back());
    }

    void VM::Stack::panic(const std::string &msg, const std::source_location &loc) {
        if (size() == vm.config.stack_values_max) values.pop_back();
        push_str(vm.mem.gc_new_auto<String>(vm, FStr(msg, vm.mem.str_alloc())).get());
        panicked = true;
        for (Frame *frame = cur_frame; frame; frame = frame->prev_frame) {
            if (frame == cur_frame && frame->meta_ptr == &frame->fallback_meta) {
                frame->fallback_meta.filename = loc.file_name();
                frame->fallback_meta.position = {loc.line(), loc.column()};
            }
            frame->fallback_meta = *frame->meta_ptr;
            frame->meta_ptr = &frame->fallback_meta;
        }
        throw Panic();
    }

    void VM::Stack::op_panic(Operator op) {
        panic("operator is not defined for these operands");
    }

    bool VM::Stack::is_panicked() const {
        return panicked;
    }

    bool VM::Stack::has_pack() const {
        return std::any_of(values.begin(), values.end(), [](const Value &val) { return val.seps != 0; });
    }

    VM::Stack::~Stack() = default;

    void VM::Object::get_refs(const std::function<void(Allocation *)> &callback) {
        for (const auto &[key, val] : fields) val.get_ref(callback);
        for (const auto &val : values) val.get_ref(callback);
    }

    bool VM::Object::contains_field(const FStr &key) const {
        return fields.contains(key);
    }

    std::optional<VM::Value> VM::Object::get_field(const FStr &key) const {
        if (!fields.contains(key)) return std::nullopt;
        return fields.at(key);
    }

    void VM::Object::set_field(const FStr &key, Value val) {
        fields[key] = val;
    }

    const decltype(VM::Object::fields) &VM::Object::get_fields() const {
        return fields;
    }

    VM::Object::Object(VM &vm) :
            Allocation(vm),
            fields(vm.mem.std_alloc<std::pair<const FStr, Value>>()),
            values(vm.mem.std_alloc<Value>()) {
    }

    void VM::Object::init_values(const funscript::VM::Value *beg, const funscript::VM::Value *end) {
        values.assign(beg, end);
    }

    bool VM::Object::contains_field(const char *key) const {
        return fields.contains(FStr(key, vm.mem.str_alloc()));
    }

    std::optional<VM::Value> VM::Object::get_field(const char *key) const {
        FStr key_f(key, vm.mem.str_alloc());
        if (!fields.contains(key_f)) return std::nullopt;
        return fields.at(key_f);
    }

    const decltype(VM::Object::values) &VM::Object::get_values() const {
        return values;
    }

    void VM::Scope::get_refs(const std::function<void(Allocation *)> &callback) {
        callback(vars);
        callback(prev_scope);
    }

    std::optional<VM::Value> VM::Scope::get_var(const funscript::FStr &name) const {
        if (vars->contains_field(name)) return vars->get_field(name);
        if (prev_scope == nullptr) return std::nullopt;
        return prev_scope->get_var(name);
    }

    bool VM::Scope::set_var(const FStr &name, Value val) { // NOLINT(readability-make-member-function-const)
        if (vars->contains_field(name)) return vars->set_field(name, val), true;
        if (prev_scope == nullptr) return false;
        return prev_scope->set_var(name, val);
    }

    VM::Scope::Scope(VM::Object *vars, VM::Scope *prev_scope) : Allocation(vars->vm), vars(vars),
                                                                prev_scope(prev_scope) {}

    VM::Frame::Frame(Function *fun, Frame *prev_frame) : Allocation(fun->vm), fun(fun), prev_frame(prev_frame),
                                                         depth(prev_frame ? prev_frame->depth + 1 : 0) {}

    void VM::Frame::get_refs(const std::function<void(Allocation *)> &callback) {
        callback(fun);
        callback(prev_frame);
    }

    VM::Bytecode::Bytecode(VM &vm, std::string data) : Allocation(vm), bytes(std::move(data)) {}

    void VM::Bytecode::get_refs(const std::function<void(Allocation *)> &callback) {}

    void VM::BytecodeFunction::get_refs(const std::function<void(Allocation *)> &callback) {
        VM::Function::get_refs(callback);
        callback(bytecode);
        callback(scope);
    }

    void VM::BytecodeFunction::call(VM::Stack &stack) {
        stack.exec_bytecode(mod, scope, bytecode, offset);
    }

    VM::BytecodeFunction::BytecodeFunction(VM &vm, Module *mod, Scope *scope, Bytecode *bytecode, size_t offset) :
            Function(vm, mod),
            scope(scope),
            bytecode(bytecode),
            offset(offset) {}

    FStr VM::BytecodeFunction::display() const {
        if (get_name().has_value()) return "function " + get_name().value();
        return "function(" + addr_to_string(this, scope->vars->vm.mem.str_alloc()) + ")";
    }

    VM::NativeFunction::NativeFunction(VM &vm, Module *mod, decltype(fn) fn) :
            Function(vm, mod), fn(std::move(fn)) {}

    void VM::NativeFunction::get_refs(const std::function<void(Allocation *)> &callback) {
        VM::Function::get_refs(callback);
    }

    FStr VM::NativeFunction::display() const {
        if (get_name().has_value()) return "function #[native]# " + get_name().value();
        return "function(#[native]# " + addr_to_string(this, vm.mem.str_alloc()) + ")";
    }

    void VM::NativeFunction::call(VM::Stack &stack) {
        return fn(stack);
    }

    VM::String::String(VM &vm, FStr bytes) : Allocation(vm), bytes(std::move(bytes)) {}

    void VM::String::get_refs(const std::function<void(Allocation *)> &callback) {}

    void VM::Array::get_refs(const std::function<void(Allocation *)> &callback) {
        for (const auto &val : values) val.get_ref(callback);
    }

    VM::Array::Array(VM &vm, funscript::VM::Value *beg, size_t len) : Allocation(vm),
                                                                      values(len, Value{Type::NUL},
                                                                             vm.mem.std_alloc<Value>()) {
        std::copy(beg, beg + len, values.data());
    }

    VM::Array::Array(funscript::VM &vm, size_t len) : Allocation(vm),
                                                      values(len, {Type::NUL}, vm.mem.std_alloc<Value>()) {}

    VM::Value &VM::Array::operator[](size_t pos) {
        return values[pos];
    }

    const VM::Value &VM::Array::operator[](size_t pos) const {
        return values[pos];
    }

    VM::Value *VM::Array::begin() {
        return values.data();
    }

    const VM::Value *VM::Array::begin() const {
        return values.data();
    }

    VM::Value *VM::Array::end() {
        return values.data() + len();
    }

    const VM::Value *VM::Array::end() const {
        return values.data() + len();
    }

    size_t VM::Array::len() const {
        return values.size();
    }

    VM::StackOverflowError::StackOverflowError() = default;

    VM::Function::Function(VM &vm, Module *mod) : Allocation(vm), name(std::nullopt), mod(mod) {}

    const std::optional<FStr> &VM::Function::get_name() const {
        return name;
    }

    void VM::Function::assign_name(const FStr &as_name) {
        if (name.has_value()) assertion_failed("function name reassignment");
        name = as_name;
    }

    void VM::Function::get_refs(const std::function<void(Allocation *)> &callback) {
        callback(mod);
    }

    void VM::Module::get_refs(const std::function<void(Allocation *)> &callback) {
        callback(globals);
        for (const auto &[alias, mod] : deps) callback(mod);
    }

    VM::Module::Module(VM &vm, VM::Object *globals, VM::Object *object) : Allocation(vm), globals(globals),
                                                                          object(object),
                                                                          deps(vm.mem.std_alloc<decltype(deps)::value_type>()) {

    }

    void VM::Module::register_dependency(const funscript::FStr &alias, funscript::VM::Module *mod) {
        deps.insert({alias, mod});
    }

    std::optional<VM::Module *> VM::Module::get_dependency(const funscript::FStr &alias) {
        if (!deps.contains(alias)) return std::nullopt;
        return deps.at(alias);
    }

    VM::Value::Value(Type type, VM::Value::Data data) : type(type), data(data), seps(0) {

    }

    ValueHolder::ValueHolder(const VM::Value &val) : val(val) {
        val.get_ref([](Allocation *ref) { ref->vm.mem.gc_pin(ref); });
    }

    ValueHolder::ValueHolder(const funscript::ValueHolder &val) : ValueHolder(val.val) {}

    ValueHolder::ValueHolder(funscript::ValueHolder &&val) noexcept: val(val.val) {
        val.val = Type::NUL;
    }

    const VM::Value &ValueHolder::get() const {
        return val;
    }

    ValueHolder &ValueHolder::operator=(funscript::ValueHolder &&val1) noexcept {
        val.get_ref([](Allocation *ref) { ref->vm.mem.gc_unpin(ref); });
        val = val1.val;
        val1.val = Type::NUL;
        return *this;
    }

    ValueHolder &ValueHolder::operator=(const funscript::ValueHolder &val1) {
        val.get_ref([](Allocation *ref) { ref->vm.mem.gc_unpin(ref); });
        val = val1.val;
        return *this;
    }

    ValueHolder::~ValueHolder() {
        val.get_ref([](Allocation *ref) { ref->vm.mem.gc_unpin(ref); });
    }
}