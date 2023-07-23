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

    const VM::Value &VM::Stack::operator[](VM::Stack::pos_t pos) const {
        if (pos < 0) pos += size();
        return values[pos];
    }

    void VM::Stack::push_sep() { return push({Type::SEP}); }

    void VM::Stack::push_int(fint num) { return push({Type::INT, {.num = num}}); }

    void VM::Stack::push_flp(fflp flp) { return push({Type::FLP, {.flp = flp}}); }

    void VM::Stack::push_obj(Object *obj) { return push({Type::OBJ, {.obj = obj}}); }

    void VM::Stack::push_fun(Function *fun) { return push({Type::FUN, {.fun = fun}}); }

    void VM::Stack::push_bln(fbln bln) { return push({Type::BLN, {.bln = bln}}); }

    void VM::Stack::push_str(String *str) { return push({Type::STR, {.str = str}}); }

    void VM::Stack::push_arr(VM::Array *arr) { return push({Type::ARR, {.arr = arr}}); }

    void VM::Stack::push_ptr(Allocation *ptr) { return push({Type::PTR, {.ptr = ptr}}); }

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
        const auto *bytecode = bytecode_obj->bytes.data();
        const auto *ip = reinterpret_cast<const Instruction *>(bytecode + offset);
        auto cur_scope = MemoryManager::AutoPtr(scope);
        const char *meta_chunk = nullptr;
        code_met_t meta{.filename = nullptr, .position = {0, 0}, .scope = nullptr};
        try {
            while (true) {
                if (kbd_int) {
                    kbd_int = 0;
                    panic("keyboard interrupt");
                }
                Instruction ins = *ip;
                if (meta_chunk && ins.meta) {
                    meta.position = *reinterpret_cast<const code_pos_t *>(meta_chunk + ins.meta);
                    meta.scope = cur_scope.get();
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
                        cur_scope->vars->init_values(values.data() + find_sep() + 1, values.data() + size());
                        pop(find_sep());
                        push_obj(cur_scope->vars);
                        ip++;
                        break;
                    }
                    case Opcode::WRP: {
                        auto obj = vm.mem.gc_new_auto<Object>(vm);
                        std::reverse(values.data() + find_sep() + 1, values.data() + size());
                        obj->init_values(values.data() + find_sep() + 1, values.data() + size());
                        pop(find_sep() + 1);
                        push_obj(obj.get());
                        ip++;
                        break;
                    }
                    case Opcode::SEP: {
                        push_sep();
                        ip++;
                        break;
                    }
                    case Opcode::IND: {
                        if (get(-1).type != Type::OBJ || get(-2).type != Type::SEP) {
                            panic("single object expected");
                        }
                        auto obj = MemoryManager::AutoPtr(get(-1).data.obj);
                        pop(-2);
                        if (obj->get_values().size() <= ins.u64) {
                            panic("object index out of range");
                        }
                        push(obj->get_values()[ins.u64]);
                        ip++;
                        break;
                    }
                    case Opcode::HAS: {
                        FStr name(reinterpret_cast<const FStr::value_type *>(bytecode + ins.u64), vm.mem.str_alloc());
                        if (get(-1).type == Type::SEP) {
                            pop();
                            push_bln(cur_scope->vars->get_field(name).has_value());
                        } else {
                            if (get(-1).type != Type::OBJ) {
                                panic("only objects are able to be indexed");
                            }
                            auto obj = MemoryManager::AutoPtr(get(-1).data.obj);
                            pop();
                            if (get(-1).type != Type::SEP) {
                                panic("can't index multiple values");
                            }
                            pop();
                            push_bln(obj->get_field(name).has_value());
                        }
                        ip++;
                        break;
                    }
                    case Opcode::GET: {
                        FStr name(reinterpret_cast<const FStr::value_type *>(bytecode + ins.u64), vm.mem.str_alloc());
                        if (get(-1).type == Type::SEP) {
                            pop();
                            auto var = cur_scope->vars->get_field(name);
                            if (!var.has_value()) {
                                panic("no such field: '" + std::string(name) + "'");
                            }
                            push(var.value());
                        } else {
                            if (get(-1).type != Type::OBJ) {
                                panic("only objects are able to be indexed");
                            }
                            auto obj = MemoryManager::AutoPtr(get(-1).data.obj);
                            pop();
                            if (get(-1).type != Type::SEP) {
                                panic("can't index multiple values");
                            }
                            pop();
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
                        if (get(-1).type == Type::SEP) {
                            pop();
                            if (get(-1).type == Type::SEP) panic("not enough values");
                            if (get(-1).type == Type::FUN && !get(-1).data.fun->get_name().has_value()) {
                                get(-1).data.fun->assign_name(name);
                            }
                            cur_scope->vars->set_field(name, get(-1));
                            pop();
                        } else {
                            if (get(-1).type != Type::OBJ) {
                                panic("only objects are able to be indexed");
                            }
                            auto obj = MemoryManager::AutoPtr(get(-1).data.obj);
                            pop();
                            if (get(-1).type != Type::SEP) return panic("can't index multiple values");
                            pop();
                            if (get(-1).type == Type::SEP) return panic("not enough values");
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
                            panic("no such variable: '" + std::string(name) + "'");
                        }
                        push(var.value());
                        ip++;
                        break;
                    }
                    case Opcode::VST: {
                        FStr name(reinterpret_cast<const FStr::value_type *>(bytecode + ins.u64), vm.mem.str_alloc());
                        if (get(-1).type == Type::SEP) return panic("not enough values");
                        if (!cur_scope->set_var(name, get(-1))) {
                            panic("no such variable: '" + std::string(name) + "'");
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
                    case Opcode::OSC: {
                        if (get(-1).type != Type::OBJ || get(-2).type != Type::SEP) {
                            panic("single object expected");
                        }
                        cur_scope = vm.mem.gc_new_auto<Scope>(get(-1).data.obj, cur_scope.get());
                        pop(-2);
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
                        if (get(-1).type != Type::BLN || get(-2).type != Type::SEP) {
                            panic("single boolean expected");
                        }
                        if (!get(-1).data.bln) ip = reinterpret_cast<const Instruction *>(bytecode + ins.u64);
                        else ip++;
                        pop(-2);
                        break;
                    }
                    case Opcode::JYS: {
                        if (get(-1).type != Type::BLN || get(-2).type != Type::SEP) {
                            panic("single boolean expected");
                        }
                        if (get(-1).data.bln) ip = reinterpret_cast<const Instruction *>(bytecode + ins.u64);
                        else ip++;
                        pop(-2);
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
                    case Opcode::EXT: {
                        if (get(-1).type != Type::OBJ) panic("object expected");
                        auto obj = MemoryManager::AutoPtr(get(-1).data.obj);
                        pop();
                        if (get(-1).type != Type::SEP) panic("too many values");
                        pop();
                        bool is_err = obj->contains_field(ERR_FLAG_NAME);
                        if (ins.u64) {
                            if (is_err) [[unlikely]] {
                                ip++;
                            } else {
                                size_t push_cnt = obj->get_values().size();
                                values.resize(size() + push_cnt);
                                std::copy(obj->get_values().data(), obj->get_values().data() + push_cnt,
                                          values.data() + size() - push_cnt);
                                ip = reinterpret_cast<const Instruction *>(bytecode + ins.u64);
                            }
                        } else {
                            if (is_err) [[unlikely]] {
                                pop(frame_start);
                                push_obj(obj.get());
                                return;
                            } else {
                                size_t push_cnt = obj->get_values().size();
                                values.resize(size() + push_cnt);
                                std::copy(obj->get_values().data(), obj->get_values().data() + push_cnt,
                                          values.data() + size() - push_cnt);
                                ip++;
                            }
                        }
                        break;
                    }
                    case Opcode::CHK: {
                        pos_t i = find_sep() + 1, j = find_sep(i - 1) + 1;
                        size_t cnt_i = size() - i, cnt_j = i - 1 - j;
                        if (cnt_j < cnt_i) panic("not enough values");
                        if (cnt_j > cnt_i && !ins.u16) panic("too many values");
                        j = pos_t(i - 1 - cnt_i);
                        for (; i < size(); i++, j++) {
                            if (get(i).type != Type::OBJ) panic("type must be an object");
                            Value fn_val = get(i).data.obj->get_field(TYPE_CHECK_NAME).value_or(Type::INT);
                            if (fn_val.type != Type::FUN) panic("type object does not provide typecheck function");
                            push_sep();
                            push_sep();
                            push(get(j));
                            call_function(fn_val.data.fun);
                            discard();
                        }
                        discard();
                        ip++;
                        break;
                    }
                }
            }
        } catch (const OutOfMemoryError &e) {
            pop(frame_start);
            scope = nullptr;
            panic("out of memory");
        } catch (const StackOverflowError &e) {
            pop(frame_start);
            panic("stack overflow");
        }
    }

    void VM::Stack::call_operator(Operator op) {
        // Calculate stack positions of operands and their lengths
        pos_t pos_a = find_sep() + 1, pos_b = find_sep(pos_a - 1) + 1;
        pos_t cnt_a = size() - pos_a, cnt_b = pos_a - pos_b - 1;
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
                if (cnt_a == 0 && cnt_b == 1 && get(pos_b).type == Type::ARR) {
                    auto arr = MemoryManager::AutoPtr(get(pos_b).data.arr);
                    pop(-3);
                    if (size() + arr->len() > vm.config.stack_values_max) throw StackOverflowError();
                    size_t pos = size();
                    values.resize(size() + arr->len());
                    std::copy(arr->begin(), arr->end(), values.data() + pos);
                    break;
                }
                if (cnt_a == 0 && cnt_b == 1 && get(pos_b).type == Type::OBJ) {
                    auto obj = MemoryManager::AutoPtr(get(pos_b).data.obj);
                    pop(-3);
                    if (size() + obj->get_values().size() > vm.config.stack_values_max) throw StackOverflowError();
                    size_t pos = size();
                    values.resize(size() + obj->get_values().size());
                    std::copy(obj->get_values().begin(), obj->get_values().end(), values.data() + pos);
                    break;
                }
                return op_panic(op);
            }
            case Operator::DIVIDE: {
                if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::INT && get(pos_b).type == Type::INT) {
                    fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                    if (b == 0) panic("division by zero");
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
                return op_panic(op);
            }
            case Operator::BW_SHL: {
                if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::INT && get(pos_b).type == Type::INT) {
                    fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                    pop(-4);
                    push_int(a << b);
                    break;
                }
                if (cnt_a == 1 && get(pos_a).type == Type::OBJ &&
                    get(pos_a).data.obj->contains_field(BW_SHL_OPERATOR_OVERLOAD_NAME)) {
                    auto fn = MemoryManager::AutoPtr<Function>(
                            get(pos_a).data.obj->get_field(BW_SHL_OPERATOR_OVERLOAD_NAME).value().data.fun);
                    pop(-2);
                    call_function(fn.get());
                    break;
                }
                return op_panic(op);
            }
            case Operator::BW_SHR: {
                if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::INT && get(pos_b).type == Type::INT) {
                    fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                    pop(-4);
                    push_int(a >> b);
                    break;
                }
                if (cnt_a == 1 && get(pos_a).type == Type::OBJ &&
                    get(pos_a).data.obj->contains_field(BW_SHR_OPERATOR_OVERLOAD_NAME)) {
                    auto fn = MemoryManager::AutoPtr<Function>(
                            get(pos_a).data.obj->get_field(BW_SHR_OPERATOR_OVERLOAD_NAME).value().data.fun);
                    pop(-2);
                    call_function(fn.get());
                    break;
                }
                return op_panic(op);
            }
            case Operator::BW_AND: {
                if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::INT && get(pos_b).type == Type::INT) {
                    fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                    pop(-4);
                    push_int(a & b);
                    break;
                }
                if (cnt_a == 1 && get(pos_a).type == Type::OBJ &&
                    get(pos_a).data.obj->contains_field(BW_AND_OPERATOR_OVERLOAD_NAME)) {
                    auto fn = MemoryManager::AutoPtr<Function>(
                            get(pos_a).data.obj->get_field(BW_AND_OPERATOR_OVERLOAD_NAME).value().data.fun);
                    pop(-2);
                    call_function(fn.get());
                    break;
                }
                return op_panic(op);
            }
            case Operator::BW_XOR: {
                if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::INT && get(pos_b).type == Type::INT) {
                    fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                    pop(-4);
                    push_int(a ^ b);
                    break;
                }
                if (cnt_a == 1 && get(pos_a).type == Type::OBJ &&
                    get(pos_a).data.obj->contains_field(BW_XOR_OPERATOR_OVERLOAD_NAME)) {
                    auto fn = MemoryManager::AutoPtr<Function>(
                            get(pos_a).data.obj->get_field(BW_XOR_OPERATOR_OVERLOAD_NAME).value().data.fun);
                    pop(-2);
                    call_function(fn.get());
                    break;
                }
                return op_panic(op);
            }
            case Operator::BW_OR: {
                if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::INT && get(pos_b).type == Type::INT) {
                    fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                    pop(-4);
                    push_int(a | b);
                    break;
                }
                if (cnt_a == 1 && get(pos_a).type == Type::OBJ &&
                    get(pos_a).data.obj->contains_field(BW_OR_OPERATOR_OVERLOAD_NAME)) {
                    auto fn = MemoryManager::AutoPtr<Function>(
                            get(pos_a).data.obj->get_field(BW_OR_OPERATOR_OVERLOAD_NAME).value().data.fun);
                    pop(-2);
                    call_function(fn.get());
                    break;
                }
                return op_panic(op);
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
                return op_panic(op);
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
                    return op_panic(op);
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
                return op_panic(op);
            }
            case Operator::CALL: {
                if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::ARR && get(pos_b).type == Type::ARR) {
                    MemoryManager::AutoPtr<Array> arr(get(pos_a).data.arr);
                    MemoryManager::AutoPtr<Array> ind(get(pos_b).data.arr);
                    pop(-4);
                    pos_t beg = size();
                    for (const auto &val : *ind) {
                        if (val.type != Type::INT || val.data.num < 0 || arr->len() <= val.data.num) {
                            panic("invalid array index");
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
                return op_panic(op);
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
                return op_panic(op);
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
                if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::OBJ && get(pos_b).type == Type::OBJ) {
                    auto obj_a = MemoryManager::AutoPtr<Object>(get(pos_a).data.obj);
                    auto obj_b = MemoryManager::AutoPtr<Object>(get(pos_b).data.obj);
                    pop(-4);
                    if (obj_a->get_values().size() != obj_b->get_values().size()) {
                        push_bln(false);
                        break;
                    }
                    if (obj_a->get_fields().size() != obj_b->get_fields().size()) {
                        push_bln(false);
                        break;
                    }
                    for (size_t pos = 0; pos < obj_a->get_values().size(); pos++) {
                        push_sep();
                        push_sep();
                        push(obj_b->get_values()[pos]);
                        push_sep();
                        push(obj_a->get_values()[pos]);
                        call_operator(Operator::EQUALS);
                        if (get(-1).type != Type::BLN || get(-2).type != Type::SEP) panic("boolean expected");
                        fbln result = get(-1).data.bln;
                        pop(-2);
                        if (!result) {
                            push_bln(false);
                            break;
                        }
                    }
                    for (const auto &[key, val] : obj_a->get_fields()) {
                        if (!obj_b->contains_field(key)) {
                            push_bln(false);
                            break;
                        }
                        push_sep();
                        push_sep();
                        push(obj_b->get_field(key).value());
                        push_sep();
                        push(val);
                        call_operator(Operator::EQUALS);
                        if (get(-1).type != Type::BLN || get(-2).type != Type::SEP) panic("boolean expected");
                        fbln result = get(-1).data.bln;
                        pop(-2);
                        if (!result) {
                            push_bln(false);
                            break;
                        }
                    }
                    push_bln(true);
                    break;
                }
                return op_panic(op);
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
                return op_panic(op);
            }
            case Operator::NOT: {
                if (cnt_a == 0 && cnt_b == 1 && get(pos_b).type == Type::BLN) {
                    fbln bln = get(pos_b).data.bln;
                    pop(-3);
                    push_bln(!bln);
                    break;
                }
                return op_panic(op);
            }
            case Operator::BW_NOT: {
                if (cnt_a == 0 && cnt_b == 1 && get(pos_b).type == Type::INT) {
                    fint num = get(pos_b).data.num;
                    pop(-3);
                    push_int(~num);
                    break;
                }
                return op_panic(op);
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
                return op_panic(op);
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
                return op_panic(op);
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
                return op_panic(op);
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
                return op_panic(op);
            }
            case Operator::IS: {
                fbln same = cnt_a == cnt_b &&
                            std::memcmp(values.data() + pos_a, values.data() + pos_b, sizeof(Value) * cnt_a) == 0;
                pop(pos_b - 1);
                push_bln(same);
                break;
            }
            case Operator::SIZEOF: {
                if (cnt_a == 0 && cnt_b == 1 && get(pos_b).type == Type::ARR) {
                    fint size = fint(get(pos_b).data.arr->len());
                    pop(-3);
                    push_int(size);
                    break;
                }
                if (cnt_a == 0 && cnt_b == 1 && get(pos_b).type == Type::OBJ) {
                    if (get(pos_b).data.obj->contains_field(SIZEOF_OPERATOR_OVERLOAD_NAME)) {
                        auto fn = MemoryManager::AutoPtr<Function>(
                                get(pos_b).data.obj->get_field(SIZEOF_OPERATOR_OVERLOAD_NAME).value().data.fun);
                        pop(-2);
                        call_function(fn.get());
                        break;
                    } else {
                        fint size = fint(get(pos_b).data.obj->get_values().size());
                        pop(-3);
                        push_int(size);
                    }
                    break;
                }
                if (cnt_a == 0 && cnt_b == 1 && get(pos_b).type == Type::STR) {
                    fint size = fint(get(pos_b).data.str->bytes.size());
                    pop(-3);
                    push_int(size);
                    break;
                }
                return op_panic(op);
            }
            default:
                assertion_failed("unknown operator");
        }
    }

    void VM::Stack::call_assignment() {
        // Calculate stack positions of operands and their lengths
        pos_t pos_a = find_sep() + 1, pos_b = find_sep(pos_a - 1) + 1;
        pos_t cnt_a = size() - pos_a, cnt_b = pos_a - pos_b - 1;
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
                    panic("invalid array index");
                }
                if (get(-1).type == Type::SEP) {
                    vm.mem.gc_unpin(&arr);
                    vm.mem.gc_unpin(&ind);
                    panic("not enough values");
                }
                arr[val.data.num] = get(-1);
                pop();
            }
            vm.mem.gc_unpin(&arr);
            vm.mem.gc_unpin(&ind);
            return;
        }
        return op_panic(Operator::CALL);
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

    void VM::Stack::panic(const std::string &msg, const std::source_location &loc) {
        if (size() == vm.config.stack_values_max) pop();
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

    VM::Frame *VM::Stack::get_current_frame() const {
        return cur_frame;
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

    const VM::code_met_t &VM::Frame::get_meta() const {
        return *meta_ptr;
    }

    VM::Bytecode::Bytecode(VM &vm, std::string data) : Allocation(vm), bytes(std::move(data)) {}

    void VM::Bytecode::get_refs(const std::function<void(Allocation *)> &callback) {}

    void VM::BytecodeFunction::get_refs(const std::function<void(Allocation *)> &callback) {
        VM::Function::get_refs(callback);
        callback(bytecode);
        callback(scope);
    }

    void VM::BytecodeFunction::call(VM::Stack &stack) {
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
                                                                      values(len, Type::INT,
                                                                             vm.mem.std_alloc<Value>()) {
        std::copy(beg, beg + len, values.data());
    }

    VM::Array::Array(funscript::VM &vm, size_t len) : Allocation(vm),
                                                      values(len, Type::INT, vm.mem.std_alloc<Value>()) {}

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

    VM::Module::Module(VM &vm, const FStr &name, VM::Object *globals, VM::Object *object) : Allocation(vm),
                                                                                            name(name),
                                                                                            globals(globals),
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

    VM::Value::Value(funscript::Type type, funscript::VM::Value::Data data) : type(type), data(data) {}

    void VM::Value::get_ref(const std::function<void(Allocation *)> &callback) const {
        if (type == Type::OBJ) callback(data.obj);
        if (type == Type::FUN) callback(data.fun);
        if (type == Type::STR) callback(data.str);
        if (type == Type::ARR) callback(data.arr);
        if (type == Type::PTR) callback(data.ptr);
    }
}