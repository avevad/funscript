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
        for (auto *frame : frames) callback(frame);
    }

    VM::Stack::Stack(VM &vm, Function *start) : Allocation(vm), values(vm.mem.std_alloc<Value>()),
                                                frames(vm.mem.std_alloc<Frame *>()) {
        frames.push_back(vm.mem.gc_new_auto<Frame>(start).get());
        push_sep(); // Empty arguments for the start function.
    }

    VM::Stack::Stack(funscript::VM &vm) : Allocation(vm), values(vm.mem.std_alloc<Value>()),
                                          frames(vm.mem.std_alloc<Frame *>()) {}

    VM::Stack::pos_t VM::Stack::size() const {
        return values.size(); // NOLINT(cppcoreguidelines-narrowing-conversions)
    }

    const VM::Value &VM::Stack::operator[](VM::Stack::pos_t pos) const {
        if (pos < 0) pos += size();
        return values[pos];
    }

    void VM::Stack::push_sep() { return push({Type::SEP}); }

    void VM::Stack::push_nul() { return push({Type::NUL}); }

    void VM::Stack::push_int(fint num) { return push({Type::INT, num}); }

    void VM::Stack::push_flp(fflp flp) { return push({Type::FLP, {.flp = flp}}); }

    void VM::Stack::push_obj(Object *obj) { return push({Type::OBJ, {.obj = obj}}); }

    void VM::Stack::push_fun(Function *fun) { return push({Type::FUN, {.fun = fun}}); }

    void VM::Stack::push_bln(fbln bln) { return push({Type::BLN, {.bln = bln}}); }

    void VM::Stack::push_str(String *str) { return push({Type::STR, {.str = str}}); }

    void VM::Stack::push_err(Error *err) { return push({Type::ERR, {.err = err}}); }

    void VM::Stack::push_arr(VM::Array *arr) { return push({Type::ARR, {.arr = arr}}); }

    void VM::Stack::push_ptr(Allocation *ptr) { return push({Type::PTR, {.ptr = ptr}}); }

    void VM::Stack::as_boolean() {
        if (get(-1).type != Type::BLN || get(-2).type != Type::SEP) {
            return raise_err("no implicit conversion to boolean", find_sep());
        }
        bool bln = get(-1).data.bln;
        pop(find_sep());
        push_bln(bln);
    }

    bool VM::Stack::discard() {
        bool res = values.back().type != Type::SEP;
        pop(find_sep());
        return res;
    }

    void VM::Stack::pop(VM::Stack::pos_t pos) {
        if (pos < 0) pos += size();
        values.resize(pos);
    }

    VM::Stack::pos_t VM::Stack::find_sep(funscript::VM::Stack::pos_t before) {
        pos_t pos = before - 1;
        while (get(pos).type != Type::SEP) pos--;
        if (pos < 0) pos += size();
        return pos;
    }

    void VM::Stack::push(const Value &e) {
        if (values.size() >= vm.config.stack_values_max) throw StackOverflowError();
        values.push_back(e);
    }

    VM::Value &VM::Stack::get(VM::Stack::pos_t pos) {
        if (pos < 0) pos += size();
        return values[pos];
    }

    volatile sig_atomic_t VM::Stack::kbd_int = 0;

    void VM::Stack::exec_bytecode(Module *mod, Scope *scope, Bytecode *bytecode_obj, size_t offset, pos_t frame_start) {
        try {
            Frame *frame = frames.back();
            const auto *bytecode = bytecode_obj->bytes.data();
            const auto *ip = reinterpret_cast<const Instruction *>(bytecode + offset);
            auto cur_scope = MemoryManager::AutoPtr(scope);
            const char *meta_chunk = nullptr;
            code_met_t meta{.filename = nullptr};
            while (true) {
                if (kbd_int) {
                    kbd_int = 0;
                    return raise_err("keyboard interrupt", frame_start);
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
                        frame->meta_ptr = &meta;
                        ip++;
                        break;
                    }
                    case Opcode::VAL: {
                        Value val{.type = static_cast<Type>(ins.u16), .data{.num = static_cast<fint>(ins.u64)}};
                        if (val.type == Type::OBJ) val.data.obj = cur_scope->vars;
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
                    case Opcode::SEP: {
                        push_sep();
                        ip++;
                        break;
                    }
                    case Opcode::GET: {
                        FStr name(reinterpret_cast<const FStr::value_type *>(bytecode + ins.u64), vm.mem.str_alloc());
                        if (get(-1).type == Type::SEP) {
                            pop();
                            auto var = cur_scope->vars->get_field(name);
                            if (!var.has_value()) {
                                return raise_err("no such field: '" + std::string(name) + "'", frame_start);
                            }
                            push(var.value());
                        } else {
                            if (get(-1).type != Type::OBJ) {
                                return raise_err("only objects are able to be indexed", frame_start);
                            }
                            auto obj = MemoryManager::AutoPtr(get(-1).data.obj);
                            pop();
                            if (get(-1).type != Type::SEP) {
                                return raise_err("can't index multiple values", frame_start);
                            }
                            pop();
                            auto field = obj->get_field(name);
                            if (!field.has_value()) {
                                return raise_err("no such field: '" + std::string(name) + "'", frame_start);
                            }
                            push(field.value());
                        }
                        ip++;
                        break;
                    }
                    case Opcode::SET: {
                        FStr name(reinterpret_cast<const FStr::value_type *>(bytecode + ins.u64), vm.mem.str_alloc());
                        if (get(-1).type == Type::SEP) {
                            pop();
                            if (get(-1).type == Type::SEP) return raise_err("not enough values", frame_start);
                            if (get(-1).type == Type::FUN && !get(-1).data.fun->get_name().has_value()) {
                                get(-1).data.fun->assign_name(name);
                            }
                            cur_scope->vars->set_field(name, get(-1));
                            pop();
                        } else {
                            if (get(-1).type != Type::OBJ) {
                                return raise_err("only objects are able to be indexed", frame_start);
                            }
                            auto obj = MemoryManager::AutoPtr(get(-1).data.obj);
                            pop();
                            if (get(-1).type != Type::SEP) return raise_err("can't index multiple values", frame_start);
                            pop();
                            if (get(-1).type == Type::SEP) return raise_err("not enough values", frame_start);
                            if (get(-1).type == Type::FUN && !get(-1).data.fun->get_name().has_value()) {
                                get(-1).data.fun->assign_name(name);
                            }
                            obj->set_field(name, get(-1));
                            pop();
                        }
                        ip++;
                        break;
                    }
                    case Opcode::VGT: {
                        FStr name(reinterpret_cast<const FStr::value_type *>(bytecode + ins.u64), vm.mem.str_alloc());
                        auto var = cur_scope->get_var(name);
                        if (!var.has_value()) {
                            return raise_err("no such variable: '" + std::string(name) + "'", frame_start);
                        }
                        push(var.value());
                        ip++;
                        break;
                    }
                    case Opcode::VST: {
                        FStr name(reinterpret_cast<const FStr::value_type *>(bytecode + ins.u64), vm.mem.str_alloc());
                        if (get(-1).type == Type::SEP) return raise_err("not enough values", frame_start);
                        if (!cur_scope->set_var(name, get(-1))) {
                            return raise_err("no such variable: '" + std::string(name) + "'", frame_start);
                        }
                        if (get(-1).type == Type::FUN && !get(-1).data.fun->get_name().has_value()) {
                            get(-1).data.fun->assign_name(name);
                        }
                        pop();
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
                        if (discard() && ins.u16) return raise_err("too many values", frame_start);
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
                        if (size() != 0 && get(-1).type == Type::ERR) {
                            auto err = MemoryManager::AutoPtr(get(-1).data.err);
                            pop(frame_start);
                            push_err(err.get());
                            return;
                        }
                        ip++;
                        break;
                    }
                    case Opcode::END: {
                        return;
                    }
                    case Opcode::JNO: {
                        as_boolean();
                        if (get(-1).type == Type::ERR) {
                            auto err = MemoryManager::AutoPtr(get(-1).data.err);
                            pop(frame_start);
                            push_err(err.get());
                            return;
                        }
                        if (!get(-1).data.bln) ip = reinterpret_cast<const Instruction *>(bytecode + ins.u64);
                        else ip++;
                        pop();
                        break;
                    }
                    case Opcode::JYS: {
                        as_boolean();
                        if (get(-1).type == Type::ERR) {
                            auto err = MemoryManager::AutoPtr(get(-1).data.err);
                            pop(frame_start);
                            push_err(err.get());
                            return;
                        }
                        if (get(-1).data.bln) ip = reinterpret_cast<const Instruction *>(bytecode + ins.u64);
                        else ip++;
                        pop();
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
                        pos_t beg = find_sep() + 1;
                        auto arr = vm.mem.gc_new_auto<Array>(vm, values.data() + beg, size() - beg);
                        pop(beg - 1);
                        ip++;
                        push_arr(arr.get());
                        break;
                    }
                    case Opcode::MOV: {
                        call_assignment();
                        if (get(-1).type == Type::ERR) {
                            auto err = MemoryManager::AutoPtr(get(-1).data.err);
                            pop(frame_start);
                            push_err(err.get());
                            return;
                        }
                        ip++;
                        break;
                    }
                    case Opcode::DUP: {
                        duplicate();
                        ip++;
                        break;
                    }
                    case Opcode::REM: {
                        remove();
                        ip++;
                        break;
                    }
                    case Opcode::INS: {
                        insert_sep();
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
                        if (get(-1).type == Type::ERR) {
                            auto err = MemoryManager::AutoPtr(get(-1).data.err);
                            pop(frame_start);
                            push_err(err.get());
                            return;
                        }
                        ip++;
                        break;
                    }
                }
            }
        } catch (const OutOfMemoryError &e) {
            return raise_err("out of memory", frame_start);
        } catch (const StackOverflowError &e) {
            return raise_err("stack overflow", frame_start);
        }
    }

    void VM::Stack::call_operator(Operator op) {
        // Calculate stack positions of operands and their lengths
        pos_t pos_a = find_sep() + 1, pos_b = find_sep(pos_a - 1) + 1;
        pos_t cnt_a = size() - pos_a, cnt_b = pos_a - pos_b - 1;
        try {
            switch (op) {
                case Operator::TIMES: {
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::INT && get(pos_b).type == Type::INT) {
                        fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                        pop(-4);
                        push_int(a * b);
                        break;
                    }
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::FLP && get(pos_b).type == Type::FLP) {
                        fflp a = get(pos_a).data.flp, b = get(pos_b).data.flp;
                        pop(-4);
                        push_flp(a * b);
                        break;
                    }
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::ARR && get(pos_b).type == Type::INT) {
                        fint k = get(pos_b).data.num;
                        size_t len = get(pos_a).data.arr->len();
                        Value *src = get(pos_a).data.arr->begin();
                        auto dst = vm.mem.gc_new_auto<Array>(vm, len * k);
                        for (size_t pos = 0; pos < len * k; pos += len) std::copy(src, src + len, dst->begin() + pos);
                        pop(-4);
                        push_arr(dst.get());
                        break;
                    }
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::INT && get(pos_b).type == Type::ARR) {
                        fint k = get(pos_a).data.num;
                        size_t len = get(pos_b).data.arr->len();
                        Value *src = get(pos_b).data.arr->begin();
                        auto dst = vm.mem.gc_new_auto<Array>(vm, len * k);
                        for (size_t pos = 0; pos < len * k; pos += len) std::copy(src, src + len, dst->begin() + pos);
                        pop(-4);
                        push_arr(dst.get());
                        break;
                    }
                    if (cnt_a == 1 && get(pos_a).type == Type::OBJ &&
                        get(pos_a).data.obj->contains_field(TIMES_OPERATOR_OVERLOAD_NAME)) {
                        auto fn = MemoryManager::AutoPtr<Function>(
                                get(pos_a).data.obj->get_field(TIMES_OPERATOR_OVERLOAD_NAME).value().data.fun);
                        pop(-2);
                        call_function(fn.get());
                        break;
                    }
                    return raise_op_err(op);
                }
                case Operator::DIVIDE: {
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::INT && get(pos_b).type == Type::INT) {
                        fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                        if (b == 0) return raise_err("division by zero", -4);
                        pop(-4);
                        push_int(a / b);
                        break;
                    }
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::FLP && get(pos_b).type == Type::FLP) {
                        fflp a = get(pos_a).data.flp, b = get(pos_b).data.flp;
                        pop(-4);
                        push_flp(a / b);
                        break;
                    }
                    if (cnt_a == 1 && get(pos_a).type == Type::OBJ &&
                        get(pos_a).data.obj->contains_field(DIVIDE_OPERATOR_OVERLOAD_NAME)) {
                        auto fn = MemoryManager::AutoPtr<Function>(
                                get(pos_a).data.obj->get_field(DIVIDE_OPERATOR_OVERLOAD_NAME).value().data.fun);
                        pop(-2);
                        call_function(fn.get());
                        break;
                    }
                    return raise_op_err(op);
                }
                case Operator::PLUS: {
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::STR && get(pos_b).type == Type::STR) {
                        FStr a = get(pos_a).data.str->bytes, b = get(pos_b).data.str->bytes;
                        pop(-4);
                        auto str = vm.mem.gc_new_auto<String>(vm, a + b);
                        push_str(str.get());
                        break;
                    }
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::ARR && get(pos_b).type == Type::ARR) {
                        size_t a_len = get(pos_a).data.arr->len();
                        Value *a_dat = get(pos_a).data.arr->begin();
                        size_t b_len = get(pos_b).data.arr->len();
                        Value *b_dat = get(pos_b).data.arr->begin();
                        auto arr = vm.mem.gc_new_auto<Array>(vm, a_len + b_len);
                        std::copy(a_dat, a_dat + a_len, arr->begin());
                        std::copy(b_dat, b_dat + b_len, arr->begin() + a_len);
                        pop(-4);
                        push_arr(arr.get());
                        break;
                    }
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::INT && get(pos_b).type == Type::INT) {
                        fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                        pop(-4);
                        push_int(a + b);
                        break;
                    }
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::FLP && get(pos_b).type == Type::FLP) {
                        fflp a = get(pos_a).data.flp, b = get(pos_b).data.flp;
                        pop(-4);
                        push_flp(a + b);
                        break;
                    }
                    if (cnt_a == 1 && get(pos_a).type == Type::OBJ &&
                        get(pos_a).data.obj->contains_field(PLUS_OPERATOR_OVERLOAD_NAME)) {
                        auto fn = MemoryManager::AutoPtr<Function>(
                                get(pos_a).data.obj->get_field(PLUS_OPERATOR_OVERLOAD_NAME).value().data.fun);
                        pop(-2);
                        call_function(fn.get());
                        break;
                    }
                    return raise_op_err(op);
                }
                case Operator::MINUS: {
                    if (cnt_a == 0) {
                        if (cnt_b == 1 && get(pos_b).type == Type::INT) {
                            fint num = get(pos_b).data.num;
                            pop(-3);
                            push_int(-num);
                            break;
                        }
                        if (cnt_b == 1 && get(pos_b).type == Type::FLP) {
                            fflp flp = get(pos_b).data.flp;
                            pop(-3);
                            push_flp(-flp);
                            break;
                        }
                        return raise_op_err(op);
                    }
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::INT && get(pos_b).type == Type::INT) {
                        fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                        pop(-4);
                        push_int(a - b);
                        break;
                    }
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::FLP && get(pos_b).type == Type::FLP) {
                        fflp a = get(pos_a).data.flp, b = get(pos_b).data.flp;
                        pop(-4);
                        push_flp(a - b);
                        break;
                    }
                    if (cnt_a == 1 && get(pos_a).type == Type::OBJ &&
                        get(pos_a).data.obj->contains_field(MINUS_OPERATOR_OVERLOAD_NAME)) {
                        auto fn = MemoryManager::AutoPtr<Function>(
                                get(pos_a).data.obj->get_field(MINUS_OPERATOR_OVERLOAD_NAME).value().data.fun);
                        pop(-2);
                        call_function(fn.get());
                        break;
                    }
                    return raise_op_err(op);
                }
                case Operator::CALL: {
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::ARR && get(pos_b).type == Type::ARR) {
                        MemoryManager::AutoPtr<Array> arr(get(pos_a).data.arr);
                        MemoryManager::AutoPtr<Array> ind(get(pos_b).data.arr);
                        pop(-4);
                        pos_t beg = size();
                        for (const auto &val : *ind) {
                            if (val.type != Type::INT || val.data.num < 0 || arr->len() <= val.data.num) {
                                return raise_err("invalid array index", beg);
                            }
                            push((*arr)[val.data.num]);
                        }
                        break;
                    }
                    if (cnt_a == 1 && get(pos_a).type == Type::FUN) {
                        auto fn = MemoryManager::AutoPtr(get(pos_a).data.fun);
                        pop(-2);
                        call_function(fn.get());
                        break;
                    }
                    if (cnt_a == 1 && get(pos_a).type == Type::OBJ &&
                        get(pos_a).data.obj->contains_field(CALL_OPERATOR_OVERLOAD_NAME)) {
                        auto fn = MemoryManager::AutoPtr<Function>(
                                get(pos_a).data.obj->get_field(CALL_OPERATOR_OVERLOAD_NAME).value().data.fun);
                        pop(-2);
                        call_function(fn.get());
                        break;
                    }
                    return raise_op_err(op);
                }
                case Operator::MODULO: {
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::INT && get(pos_b).type == Type::INT) {
                        fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                        pop(-4);
                        push_int(a % b);
                        break;
                    }
                    if (cnt_a == 1 && get(pos_a).type == Type::OBJ &&
                        get(pos_a).data.obj->contains_field(MODULO_OPERATOR_OVERLOAD_NAME)) {
                        auto fn = MemoryManager::AutoPtr<Function>(
                                get(pos_a).data.obj->get_field(MODULO_OPERATOR_OVERLOAD_NAME).value().data.fun);
                        pop(-2);
                        call_function(fn.get());
                        break;
                    }
                    return raise_op_err(op);
                }
                case Operator::EQUALS: {
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::INT && get(pos_b).type == Type::INT) {
                        fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                        pop(-4);
                        push_bln(a == b);
                        break;
                    }
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::FLP && get(pos_b).type == Type::FLP) {
                        fflp a = get(pos_a).data.flp, b = get(pos_b).data.flp;
                        pop(-4);
                        push_bln(a == b);
                        break;
                    }
                    if (cnt_a == 1 && get(pos_a).type == Type::OBJ &&
                        get(pos_a).data.obj->contains_field(EQUALS_OPERATOR_OVERLOAD_NAME)) {
                        auto fn = MemoryManager::AutoPtr<Function>(
                                get(pos_a).data.obj->get_field(EQUALS_OPERATOR_OVERLOAD_NAME).value().data.fun);
                        pop(-2);
                        call_function(fn.get());
                        break;
                    }
                    return raise_op_err(op);
                }
                case Operator::DIFFERS: {
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::INT && get(pos_b).type == Type::INT) {
                        fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                        pop(-4);
                        push_bln(a != b);
                        break;
                    }
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::FLP && get(pos_b).type == Type::FLP) {
                        fflp a = get(pos_a).data.flp, b = get(pos_b).data.flp;
                        pop(-4);
                        push_bln(a != b);
                        break;
                    }
                    if (cnt_a == 1 && get(pos_a).type == Type::OBJ &&
                        get(pos_a).data.obj->contains_field(DIFFERS_OPERATOR_OVERLOAD_NAME)) {
                        auto fn = MemoryManager::AutoPtr<Function>(
                                get(pos_a).data.obj->get_field(DIFFERS_OPERATOR_OVERLOAD_NAME).value().data.fun);
                        pop(-2);
                        call_function(fn.get());
                        break;
                    }
                    return raise_op_err(op);
                }
                case Operator::NOT: {
                    if (cnt_a == 0 && cnt_b == 1 && get(pos_b).type == Type::BLN) {
                        fbln bln = get(pos_b).data.bln;
                        pop(-3);
                        push_bln(!bln);
                        break;
                    }
                    return raise_op_err(op);
                }
                case Operator::LESS: {
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::INT && get(pos_b).type == Type::INT) {
                        fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                        pop(-4);
                        push_bln(a < b);
                        break;
                    }
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::FLP && get(pos_b).type == Type::FLP) {
                        fflp a = get(pos_a).data.flp, b = get(pos_b).data.flp;
                        pop(-4);
                        push_bln(a < b);
                        break;
                    }
                    if (cnt_a == 1 && get(pos_a).type == Type::OBJ &&
                        get(pos_a).data.obj->contains_field(LESS_OPERATOR_OVERLOAD_NAME)) {
                        auto fn = MemoryManager::AutoPtr<Function>(
                                get(pos_a).data.obj->get_field(LESS_OPERATOR_OVERLOAD_NAME).value().data.fun);
                        pop(-2);
                        call_function(fn.get());
                        break;
                    }
                    return raise_op_err(op);
                }
                case Operator::GREATER: {
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::INT && get(pos_b).type == Type::INT) {
                        fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                        pop(-4);
                        push_bln(a > b);
                        break;
                    }
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::FLP && get(pos_b).type == Type::FLP) {
                        fflp a = get(pos_a).data.flp, b = get(pos_b).data.flp;
                        pop(-4);
                        push_bln(a > b);
                        break;
                    }
                    if (cnt_a == 1 && get(pos_a).type == Type::OBJ &&
                        get(pos_a).data.obj->contains_field(GREATER_OPERATOR_OVERLOAD_NAME)) {
                        auto fn = MemoryManager::AutoPtr<Function>(
                                get(pos_a).data.obj->get_field(GREATER_OPERATOR_OVERLOAD_NAME).value().data.fun);
                        pop(-2);
                        call_function(fn.get());
                        break;
                    }
                    return raise_op_err(op);
                }
                case Operator::LESS_EQUAL: {
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::INT && get(pos_b).type == Type::INT) {
                        fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                        pop(-4);
                        push_bln(a <= b);
                        break;
                    }
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::FLP && get(pos_b).type == Type::FLP) {
                        fflp a = get(pos_a).data.flp, b = get(pos_b).data.flp;
                        pop(-4);
                        push_bln(a <= b);
                        break;
                    }
                    if (cnt_a == 1 && get(pos_a).type == Type::OBJ &&
                        get(pos_a).data.obj->contains_field(LESS_EQUAL_OPERATOR_OVERLOAD_NAME)) {
                        auto fn = MemoryManager::AutoPtr<Function>(
                                get(pos_a).data.obj->get_field(LESS_EQUAL_OPERATOR_OVERLOAD_NAME).value().data.fun);
                        pop(-2);
                        call_function(fn.get());
                        break;
                    }
                    return raise_op_err(op);
                }
                case Operator::GREATER_EQUAL: {
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::INT && get(pos_b).type == Type::INT) {
                        fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                        pop(-4);
                        push_bln(a >= b);
                        break;
                    }
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::FLP && get(pos_b).type == Type::FLP) {
                        fflp a = get(pos_a).data.flp, b = get(pos_b).data.flp;
                        pop(-4);
                        push_bln(a >= b);
                        break;
                    }
                    if (cnt_a == 1 && get(pos_a).type == Type::OBJ &&
                        get(pos_a).data.obj->contains_field(GREATER_EQUAL_OPERATOR_OVERLOAD_NAME)) {
                        auto fn = MemoryManager::AutoPtr<Function>(
                                get(pos_a).data.obj->get_field(GREATER_EQUAL_OPERATOR_OVERLOAD_NAME).value().data.fun);
                        pop(-2);
                        call_function(fn.get());
                        break;
                    }
                    return raise_op_err(op);
                }
                case Operator::IS: {
                    fbln same = cnt_a == cnt_b &&
                                std::memcmp(values.data() + pos_a, values.data() + pos_b, sizeof(Value) * cnt_a) == 0;
                    pop(pos_b - 1);
                    push_bln(same);
                    break;
                }
                default:
                    assertion_failed("unknown operator");
            }
        } catch (const OutOfMemoryError &e) {
            return raise_err("out of memory", pos_b - 1);
        } catch (const StackOverflowError &e) {
            return raise_err("stack overflow", pos_b - 1);
        }
    }

    void VM::Stack::call_assignment() {
        // Calculate stack positions of operands and their lengths
        pos_t pos_a = find_sep() + 1, pos_b = find_sep(pos_a - 1) + 1;
        pos_t cnt_a = size() - pos_a, cnt_b = pos_a - pos_b - 1;
        try {
            if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::ARR && get(pos_b).type == Type::ARR) {
                Array &arr = *get(pos_a).data.arr;
                Array &ind = *get(pos_b).data.arr;
                vm.mem.gc_pin(&arr);
                vm.mem.gc_pin(&ind);
                pop(-4);
                pos_t beg = size();
                for (const auto &val : ind) {
                    if (val.type != Type::INT || val.data.num < 0 || arr.len() <= val.data.num) {
                        vm.mem.gc_unpin(&arr);
                        vm.mem.gc_unpin(&ind);
                        return raise_err("invalid array index", beg);
                    }
                    if (get(-1).type == Type::SEP) {
                        vm.mem.gc_unpin(&arr);
                        vm.mem.gc_unpin(&ind);
                        return raise_err("not enough values", beg);
                    }
                    arr[val.data.num] = get(-1);
                    pop();
                }
                vm.mem.gc_unpin(&arr);
                vm.mem.gc_unpin(&ind);
                return;
            }
            return raise_op_err(Operator::CALL);
        } catch (const OutOfMemoryError &e) {
            return raise_err("out of memory", pos_b - 1);
        }
    }

    void VM::Stack::call_type_check() {
        pos_t frame_start = find_sep(find_sep() - 1);
        try {
            if (get(-1).type != Type::OBJ) return raise_err("type must be an object", frame_start);
            if (get(-2).type != Type::SEP) return raise_err("too many values", frame_start);
            auto typ = MemoryManager::AutoPtr(get(-1).data.obj);
            pop(-2);
            if (!typ->contains_field(TYPE_CHECK_NAME)) {
                return raise_err("type object does not provide type check function", frame_start);
            }
            auto fn = MemoryManager::AutoPtr(typ->get_field(TYPE_CHECK_NAME).value().data.fun);
            call_function(fn.get());
        } catch (const OutOfMemoryError &e) {
            return raise_err("out of memory", frame_start);
        }
    }

    void VM::Stack::call_function(Function *fun) {
        if (frames.size() >= vm.config.stack_frames_max) return raise_err("stack overflow", find_sep());
        auto frame = vm.mem.gc_new_auto<Frame>(fun);
        frames.push_back(frame.get());
        fun->call(*this, frame.get());
        frames.pop_back();
    }

    void VM::Stack::continue_execution() {
        if (frames.empty()) assertion_failed("the routine is no longer alive");
        for (pos_t pos = pos_t(frames.size()) - 1; pos >= 0; pos--) {
            frames[pos]->fun->call(*this, frames[pos]);
            frames.pop_back();
        }
    }

    void VM::Stack::raise_err(const std::string &msg, VM::Stack::pos_t frame_start) {
        pop(frame_start);
        try {
            auto err_obj = vm.mem.gc_new_auto<Object>(vm);
            err_obj->set_field(FStr("msg", vm.mem.str_alloc()),
                               {Type::STR,
                                {.str = vm.mem.gc_new_auto<String>(vm, FStr(msg, vm.mem.str_alloc())).get()}});
            FVec<Error::stack_trace_element> stacktrace(vm.mem.std_alloc<Error::stack_trace_element>());
            stacktrace.reserve(frames.size());
            for (const auto &frame : frames) {
                code_met_t code_meta{.filename = "?", .position = {0, 0}};
                if (frame->meta_ptr) code_meta = *frame->meta_ptr;
                if (!code_meta.filename) code_meta.filename = "?";
                stacktrace.push_back({.function_repr = frame->fun->display(), .code_meta = code_meta});
            }
            auto err = vm.mem.gc_new_auto<Error>(err_obj.get(), stacktrace);
            push_err(err.get());
        } catch (const OutOfMemoryError &) {
            assertion_failed("not enough memory to raise an error");
        }
    }

    void VM::Stack::raise_op_err(Operator op) {
        pos_t pos = find_sep(find_sep());
        raise_err("operator is not defined for these operands", pos);
    }

    void VM::Stack::reverse() {
        for (pos_t pos1 = find_sep() + 1, pos2 = size() - 1; pos1 < pos2; pos1++, pos2--) {
            std::swap(values[pos1], values[pos2]);
        }
    }

    void VM::Stack::duplicate() {
        pos_t beg = find_sep();
        pos_t len = size() - beg;
        if (size() + len > vm.config.stack_values_max) throw StackOverflowError();
        values.resize(values.size() + len);
        std::copy(values.data() + beg, values.data() + beg + len, values.data() + beg + len);
    }

    void VM::Stack::remove() {
        pos_t pos = find_sep();
        std::move(values.data() + pos + 1, values.data() + size(), values.data() + pos);
        values.pop_back();
    }

    void VM::Stack::insert_sep() {
        if (size() + 1 > vm.config.stack_values_max) throw StackOverflowError();
        values.insert(values.end() - 1, {Type::SEP});
    }

    void VM::Stack::duplicate_value() {
        if (size() + 1 > vm.config.stack_values_max) throw StackOverflowError();
        values.push_back(values.back());
    }

    VM::Stack::~Stack() = default;

    void VM::Object::get_refs(const std::function<void(Allocation *)> &callback) {
        for (const auto &[key, val] : fields) val.get_ref(callback);
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

    VM::Object::Object(VM &vm) : Allocation(vm), fields(vm.mem.std_alloc<std::pair<const FStr, Value>>()) {}

    bool VM::Object::contains_field(const char *key) const {
        return fields.contains(FStr(key, vm.mem.str_alloc()));
    }

    std::optional<VM::Value> VM::Object::get_field(const char *key) const {
        FStr key_f(key, vm.mem.str_alloc());
        if (!fields.contains(key_f)) return std::nullopt;
        return fields.at(key_f);
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

    VM::Frame::Frame(Function *fun) : Allocation(fun->vm), fun(fun) {}

    void VM::Frame::get_refs(const std::function<void(Allocation *)> &callback) {
        callback(fun);
    }

    VM::Bytecode::Bytecode(VM &vm, std::string data) : Allocation(vm), bytes(std::move(data)) {}

    void VM::Bytecode::get_refs(const std::function<void(Allocation *)> &callback) {}

    void VM::BytecodeFunction::get_refs(const std::function<void(Allocation *)> &callback) {
        VM::Function::get_refs(callback);
        callback(bytecode);
        callback(scope);
    }

    void VM::BytecodeFunction::call(VM::Stack &stack, Frame *frame) {
        stack.exec_bytecode(mod, scope, bytecode, offset, stack.find_sep());
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
        if (get_name().has_value()) return "#[native]# function " + get_name().value();
        return "#[native]# function(" + addr_to_string(this, vm.mem.str_alloc()) + ")";
    }

    void VM::NativeFunction::call(VM::Stack &stack, funscript::VM::Frame *frame) {
        return fn(stack, frame);
    }

    VM::String::String(VM &vm, FStr bytes) : Allocation(vm), bytes(std::move(bytes)) {}

    void VM::String::get_refs(const std::function<void(Allocation *)> &callback) {}

    void VM::Error::get_refs(const std::function<void(Allocation *)> &callback) {
        callback(obj);
    }

    VM::Error::Error(funscript::VM::Object *obj, const FVec<funscript::VM::Error::stack_trace_element> &stacktrace) :
            Allocation(obj->vm), obj(obj), stacktrace(stacktrace) {}

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
}