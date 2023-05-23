#include <queue>
#include <utility>
#include "vm.h"

namespace funscript {

    VM::VM(VM::Config config) : config(config), mem(config.mm) {}

    void VM::Stack::get_refs(const std::function<void(Allocation *)> &callback) {
        for (const auto &val : values) val.get_ref(callback);
        for (auto *frame : frames) callback(frame);
    }

    VM::Stack::Stack(VM &vm, Function *start) : vm(vm), values(vm.mem.std_alloc<Value>()),
                                                frames(vm.mem.std_alloc<Frame *>()) {
        auto frame = vm.mem.gc_new_auto<Frame>(*this, start);
        vm.mem.gc_add_ref(frame.get());
        frames.push_back(frame.get());
        if (!push_sep()) assertion_failed("stack size limit is too small"); // Empty arguments for the start function.
    }

    VM::Stack::pos_t VM::Stack::size() const {
        return values.size(); // NOLINT(cppcoreguidelines-narrowing-conversions)
    }

    const VM::Value &VM::Stack::operator[](VM::Stack::pos_t pos) {
        if (pos < 0) pos += size();
        return values[pos];
    }

    [[nodiscard]] bool VM::Stack::push_sep() { return push({Type::SEP}); }

    [[nodiscard]] bool VM::Stack::push_nul() { return push({Type::NUL}); }

    [[nodiscard]] bool VM::Stack::push_int(fint num) { return push({Type::INT, num}); }

    [[nodiscard]] bool VM::Stack::push_obj(Object *obj) { return push({Type::OBJ, {.obj = obj}}); }

    [[nodiscard]] bool VM::Stack::push_fun(Function *fun) { return push({Type::FUN, {.fun = fun}}); }

    [[nodiscard]] bool VM::Stack::push_bln(bool bln) { return push({Type::BLN, {.bln = bln}}); }

    [[nodiscard]] bool VM::Stack::push_str(String *str) { return push({Type::STR, {.str = str}}); }

    [[nodiscard]] bool VM::Stack::push_err(Error *err) { return push({Type::ERR, {.err = err}}); }

    [[nodiscard]] bool VM::Stack::push_arr(VM::Array *arr) { return push({Type::ARR, {.arr = arr}}); }

    void VM::Stack::as_boolean() {
        if (get(-1).type != Type::BLN || get(-2).type != Type::SEP) {
            return raise_err("no implicit conversion to boolean", find_sep());
        }
        bool bln = get(-1).data.bln;
        pop(find_sep());
        if (!push_bln(bln)) assertion_failed("failed push() after pop()");
    }

    void VM::Stack::discard() { pop(find_sep()); }

    void VM::Stack::pop(VM::Stack::pos_t pos) {
        if (pos < 0) pos += size();
        for (pos_t cur_pos = pos; cur_pos < size(); cur_pos++) {
            values[cur_pos].get_ref([this](auto *ref) { vm.mem.gc_del_ref(ref); });
        }
        values.resize(pos);
    }

    VM::Stack::pos_t VM::Stack::find_sep(funscript::VM::Stack::pos_t before) {
        pos_t pos = before - 1;
        while (get(pos).type != Type::SEP) pos--;
        if (pos < 0) pos += size();
        return pos;
    }

    bool VM::Stack::push(const Value &e) {
        if (values.size() >= vm.config.stack_values_max) return false;
        else {
            values.push_back(e);
            e.get_ref([this](auto *ref) { vm.mem.gc_add_ref(ref); });
            return true;
        }
    }

    const VM::Value &VM::Stack::get(VM::Stack::pos_t pos) {
        if (pos < 0) pos += size();
        return values[pos];
    }

    volatile sig_atomic_t VM::Stack::kbd_int = 0;

    void VM::Stack::exec_bytecode(Scope *scope, Bytecode *bytecode_obj, size_t offset, pos_t frame_start) {
        try {
            const auto *bytecode = bytecode_obj->bytes.data();
            const auto *ip = reinterpret_cast<const Instruction *>(bytecode + offset);
            auto cur_scope = MemoryManager::AutoPtr<Scope>(vm.mem, scope);
            while (true) {
                if (kbd_int) {
                    kbd_int = 0;
                    return raise_err("keyboard interrupt", frame_start);
                }
                Instruction ins = *ip;
                switch (ins.op) {
                    case Opcode::NOP:
                        ip++;
                        break;
                    case Opcode::VAL: {
                        Value val{.type = static_cast<Type>(ins.u16), .data{.num = static_cast<fint>(ins.u64)}};
                        if (val.type == Type::OBJ) val.data.obj = cur_scope->vars;
                        if (val.type == Type::FUN) {
                            auto *fun = vm.mem.gc_new<BytecodeFunction>(cur_scope.get(), bytecode_obj, size_t(ins.u64));
                            if (!fun) return raise_err("out of memory", frame_start);
                            val.data.fun = fun;
                        }
                        if (!push(val)) {
                            if (val.type == Type::FUN) vm.mem.gc_unpin(val.data.fun);
                            return raise_err("value stack overflow", frame_start);
                        }
                        if (val.type == Type::FUN) vm.mem.gc_unpin(val.data.fun);
                        ip++;
                        break;
                    }
                    case Opcode::SEP: {
                        if (!push_sep()) return raise_err("value stack overflow", frame_start);
                        ip++;
                        break;
                    }
                    case Opcode::GET: {
                        fstr name(reinterpret_cast<const fstr::value_type *>(bytecode + ins.u64), vm.mem.str_alloc());
                        if (get(-1).type == Type::SEP) {
                            pop();
                            auto var = cur_scope->get_var(name);
                            if (!var.has_value()) {
                                return raise_err("no such variable: '" + std::string(name) + "'", frame_start);
                            }
                            if (!push(var.value())) assertion_failed("failed push() after pop()");
                        } else {
                            if (get(-1).type != Type::OBJ) {
                                return raise_err("only objects are able to be indexed", frame_start);
                            }
                            Object *obj = get(-1).data.obj;
                            vm.mem.gc_pin(obj);
                            pop();
                            if (get(-1).type != Type::SEP) {
                                return raise_err("can't index multiple values", frame_start);
                            }
                            pop();
                            auto field = obj->get_field(name);
                            if (!field.has_value()) {
                                return raise_err("no such field: '" + std::string(name) + "'", frame_start);
                            }
                            if (!push(field.value())) assertion_failed("failed push() after pop()");
                            vm.mem.gc_unpin(obj);
                        }
                        ip++;
                        break;
                    }
                    case Opcode::SET: {
                        fstr name(reinterpret_cast<const fstr::value_type *>(bytecode + ins.u64), vm.mem.str_alloc());
                        if (get(-1).type == Type::SEP) {
                            pop();
                            if (get(-1).type == Type::SEP) return raise_err("not enough values to assign", frame_start);
                            cur_scope->set_var(name, get(-1));
                            pop();
                        } else {
                            if (get(-1).type != Type::OBJ) {
                                return raise_err("only objects are able to be indexed", frame_start);
                            }
                            Object *obj = get(-1).data.obj;
                            vm.mem.gc_pin(obj);
                            pop();
                            if (get(-1).type != Type::SEP) return raise_err("can't index multiple values", frame_start);
                            pop();
                            if (get(-1).type == Type::SEP) return raise_err("not enough values to assign", frame_start);
                            obj->set_field(name, get(-1));
                            pop();
                            vm.mem.gc_unpin(obj);
                        }
                        ip++;
                        break;
                    }
                    case Opcode::SCP: {
                        if (ins.u16) {
                            auto vars = vm.mem.gc_new_auto<Object>(vm);
                            if (!vars) return raise_err("out of memory", frame_start);
                            cur_scope = vm.mem.gc_new_auto<Scope>(vars.get(), cur_scope.get());
                            if (!cur_scope) return raise_err("out of memory", frame_start);
                        } else {
                            cur_scope.set(cur_scope->prev_scope);
                        }
                        ip++;
                        break;
                    }
                    case Opcode::DIS: {
                        discard();
                        ip++;
                        break;
                    }
                    case Opcode::OPR: {
                        auto op = static_cast<Operator>(ins.u16);
                        call_operator(op, nullptr);
                        if (get(-1).type == Type::ERR) {
                            Error *err = get(-1).data.err;
                            vm.mem.gc_pin(err);
                            pop(frame_start);
                            if (!push_err(err)) assertion_failed("failed push() after pop()");
                            vm.mem.gc_unpin(err);
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
                            Error *err = get(-1).data.err;
                            vm.mem.gc_pin(err);
                            pop(frame_start);
                            if (!push_err(err)) assertion_failed("failed push() after pop()");
                            vm.mem.gc_unpin(err);
                            return;
                        }
                        if (!get(-1).data.bln) ip = reinterpret_cast<const Instruction *>(bytecode + ins.u64);
                        else ip++;
                        pop();
                        break;
                    }
                    case Opcode::JMP: {
                        ip = reinterpret_cast<const Instruction *>(bytecode + ins.u64);
                        break;
                    }
                    case Opcode::STR: {
                        fstr str(reinterpret_cast<const fstr::value_type *>(bytecode + ins.u64),
                                 reinterpret_cast<const fstr::value_type *>(bytecode + ins.u64 + ins.u16),
                                 vm.mem.str_alloc());
                        auto str_obj = vm.mem.gc_new_auto<String>(str);
                        if (!str_obj) return raise_err("out of memory", frame_start);
                        if (!push_str(str_obj.get())) return raise_err("value stack overflow", frame_start);
                        ip++;
                        break;
                    }
                    case Opcode::ARR: {
                        pos_t beg = find_sep() + 1;
                        auto arr = vm.mem.gc_new_auto<Array>(vm, values.data() + beg, size() - beg);
                        if (!arr) return raise_err("out of memory", frame_start);
                        pop(beg - 1);
                        ip++;
                        if (!push_arr(arr.get())) assertion_failed("failed push() after pop()");
                        break;
                    }
                    case Opcode::MOV: {
                        call_assignment(nullptr);
                        if (get(-1).type == Type::ERR) {
                            Error *err = get(-1).data.err;
                            vm.mem.gc_pin(err);
                            pop(frame_start);
                            if (!push_err(err)) assertion_failed("failed push() after pop()");
                            vm.mem.gc_unpin(err);
                            return;
                        }
                        ip++;
                        break;
                    }
                }
            }
        } catch (const std::bad_alloc &e) {
            return raise_err("out of memory", frame_start);
        }
    }

    void VM::Stack::call_operator(Operator op, Function *cont_fn) {
        // Calculate stack positions of operands and their lengths
        pos_t pos_a = find_sep() + 1, pos_b = find_sep(pos_a - 1) + 1;
        pos_t cnt_a = size() - pos_a, cnt_b = pos_a - pos_b - 1;
        try {
            switch (op) {
                case Operator::TIMES: {
                    if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                        return raise_op_err(op);
                    }
                    fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                    pop(-4);
                    if (!push_int(a * b)) assertion_failed("failed push() after pop()");
                    break;
                }
                case Operator::DIVIDE: {
                    if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                        return raise_op_err(op);
                    }
                    fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                    if (b == 0) return raise_err("division by zero", -4);
                    pop(-4);
                    if (!push_int(a / b)) assertion_failed("failed push() after pop()");
                    break;
                }
                case Operator::PLUS: {
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::STR && get(pos_b).type == Type::STR) {
                        fstr a = get(pos_a).data.str->bytes, b = get(pos_b).data.str->bytes;
                        pop(-4);
                        auto str = vm.mem.gc_new_auto<String>(a + b);
                        if (!str) return raise_err("out of memory", pos_b - 1);
                        if (!push_str(str.get())) assertion_failed("failed push() after pop()");
                        break;
                    }
                    if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::ARR && get(pos_b).type == Type::ARR) {
                        auto arr = vm.mem.gc_new_auto<Array>(vm, get(pos_a).data.arr, get(pos_b).data.arr);
                        if (!arr) return raise_err("out of memory", pos_b - 1);
                        pop(-4);
                        if (!push_arr(arr.get())) assertion_failed("failed push() after pop()");
                        break;
                    }
                    if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                        return raise_op_err(op);
                    }
                    fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                    pop(-4);
                    if (!push_int(a + b)) assertion_failed("failed push() after pop()");
                    break;
                }
                case Operator::MINUS: {
                    if (cnt_a == 0) {
                        if (cnt_b != 1 || get(pos_b).type != Type::INT) {
                            return raise_op_err(op);
                        }
                        fint num = get(pos_b).data.num;
                        pop(-3);
                        if (!push_int(-num)) assertion_failed("failed push() after pop()");
                        break;
                    }
                    if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                        return raise_op_err(op);
                    }
                    fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                    pop(-4);
                    if (!push_int(a - b)) assertion_failed("failed push() after pop()");
                    break;
                }
                case Operator::CALL: {
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
                            if (!push(arr[val.data.num])) {
                                vm.mem.gc_unpin(&arr);
                                vm.mem.gc_unpin(&ind);
                                return raise_err("value stack overflow", beg);
                            }
                        }
                        vm.mem.gc_unpin(&arr);
                        vm.mem.gc_unpin(&ind);
                        break;
                    }
                    if (cnt_a != 1 || get(pos_a).type != Type::FUN) {
                        return raise_op_err(op);
                    }
                    Function *fn = get(pos_a).data.fun;
                    vm.mem.gc_pin(fn);
                    pop(-2);
                    call_function(fn, cont_fn);
                    vm.mem.gc_unpin(fn);
                    break;
                }
                case Operator::MODULO: {
                    if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                        return raise_op_err(op);
                    }
                    fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                    pop(-4);
                    if (!push_int(a % b)) assertion_failed("failed push() after pop()");
                    break;
                }
                case Operator::EQUALS: {
                    if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                        return raise_op_err(op);
                    }
                    fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                    pop(-4);
                    if (!push_bln(a == b)) assertion_failed("failed push() after pop()");
                    break;
                }
                case Operator::DIFFERS: {
                    if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                        return raise_op_err(op);
                    }
                    fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                    pop(-4);
                    if (!push_bln(a != b)) assertion_failed("failed push() after pop()");
                    break;
                }
                case Operator::NOT: {
                    if (cnt_a != 0 || cnt_b != 1 || get(pos_b).type != Type::INT) {
                        return raise_op_err(op);
                    }
                    bool bln = get(pos_b).data.bln;
                    pop(-3);
                    if (!push_bln(!bln)) assertion_failed("failed push() after pop()");
                    break;
                }
                case Operator::LESS: {
                    if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                        return raise_op_err(op);
                    }
                    fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                    pop(-4);
                    if (!push_bln(a < b)) assertion_failed("failed push() after pop()");
                    break;
                }
                case Operator::GREATER: {
                    if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                        return raise_op_err(op);
                    }
                    fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                    pop(-4);
                    if (!push_bln(a > b)) assertion_failed("failed push() after pop()");
                    break;
                }
                case Operator::LESS_EQUAL: {
                    if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                        return raise_op_err(op);
                    }
                    fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                    pop(-4);
                    if (!push_bln(a <= b)) assertion_failed("failed push() after pop()");
                    break;
                }
                case Operator::GREATER_EQUAL: {
                    if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                        return raise_op_err(op);
                    }
                    fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                    pop(-4);
                    if (!push_bln(a >= b)) assertion_failed("failed push() after pop()");
                    break;
                }
                default:
                    assertion_failed("unknown operator");
            }
        } catch (const std::bad_alloc &e) {
            return raise_err("out of memory", pos_b - 1);
        }
    }

    void VM::Stack::call_assignment(funscript::VM::Function *cont_fn) {
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
                        return raise_err("not enough values to assign", beg);
                    }
                    arr.set(val.data.num, get(-1));
                    pop();
                }
                vm.mem.gc_unpin(&arr);
                vm.mem.gc_unpin(&ind);
                return;
            }
            return raise_op_err(Operator::CALL);
        } catch (const std::bad_alloc &e) {
            return raise_err("out of memory", pos_b - 1);
        }
    }

    void VM::Stack::call_function(Function *fun, Function *cont_fn) {
        if (frames.size() >= vm.config.stack_frames_max) return raise_err("stack overflow", find_sep());
        frames.back()->set_cont_fn(cont_fn);
        auto frame = vm.mem.gc_new_auto<Frame>(*this, nullptr);
        if (!frame) return raise_err("out of memory", find_sep());
        frames.push_back(frame.get());
        vm.mem.gc_add_ref(frame.get());
        fun->call(*this, frame.get());
        vm.mem.gc_del_ref(frame.get());
        frames.pop_back();
        frames.back()->set_cont_fn(nullptr);
    }

    void VM::Stack::continue_execution() {
        if (frames.empty()) assertion_failed("the routine is no longer alive");
        for (pos_t pos = pos_t(frames.size()) - 1; pos >= 0; pos--) {
            Function *cont_fn = frames[pos]->get_cont_fn();
            vm.mem.gc_pin(cont_fn);
            frames[pos]->set_cont_fn(nullptr);
            cont_fn->call(*this, frames[pos]);
            vm.mem.gc_unpin(cont_fn);
        }
    }

    void VM::Stack::raise_err(const std::string &msg, VM::Stack::pos_t frame_start) {
        pop(frame_start);
        auto err = vm.mem.gc_new_auto<Error>(fstr(msg, vm.mem.std_alloc<char>()));
        if (!err) assertion_failed("not enough memory to raise an error");
        if (!push_err(err.get())) assertion_failed("value stack overflow while trying to push error object");
    }

    void VM::Stack::raise_op_err(Operator op) {
        pos_t pos = find_sep(find_sep());
        raise_err("operator is not defined for these operands", pos);
    }

    VM::Stack::~Stack() {
        for (const auto &val : values) val.get_ref([this](auto *ref) { vm.mem.gc_del_ref(ref); });
        for (auto *frame : frames) vm.mem.gc_del_ref(frame);
    }

    void VM::Object::get_refs(const std::function<void(Allocation *)> &callback) {
        for (const auto &[key, val] : fields) val.get_ref(callback);
    }

    bool VM::Object::contains_field(const fstr &key) const {
        return fields.contains(key);
    }

    std::optional<VM::Value> VM::Object::get_field(const fstr &key) const {
        if (!fields.contains(key)) return std::nullopt;
        return fields.at(key);
    }

    void VM::Object::set_field(const fstr &key, Value val) {
        if (fields.contains(key)) fields[key].get_ref([this](auto *ref) { vm.mem.gc_del_ref(ref); });
        fields[key] = val;
        val.get_ref([this](auto *ref) { vm.mem.gc_add_ref(ref); });
    }

    VM::Object::~Object() {
        for (const auto &[key, val] : fields) {
            val.get_ref([this](auto *ref) { vm.mem.gc_del_ref(ref); });
        }
    }

    void VM::Scope::get_refs(const std::function<void(Allocation *)> &callback) {
        callback(vars);
        callback(prev_scope);
    }

    void VM::Scope::set_var(const fstr &name, Value val, Scope &first) {
        if (vars->contains_field(name)) vars->set_field(name, val);
        if (prev_scope) prev_scope->set_var(name, val, first);
        else first.vars->set_field(name, val);
    }

    std::optional<VM::Value> VM::Scope::get_var(const funscript::fstr &name) const {
        if (vars->contains_field(name)) return vars->get_field(name);
        if (prev_scope == nullptr) return std::nullopt;
        return prev_scope->get_var(name);
    }

    void VM::Scope::set_var(const fstr &name, Value val) {
        set_var(name, val, *this);
    }

    VM::Scope::Scope(VM::Object *vars, VM::Scope *prev_scope) : vars(vars), prev_scope(prev_scope) {
        vars->vm.mem.gc_add_ref(vars);
        if (prev_scope) vars->vm.mem.gc_add_ref(prev_scope);
    }

    VM::Scope::~Scope() {
        if (prev_scope) vars->vm.mem.gc_del_ref(prev_scope);
        vars->vm.mem.gc_del_ref(vars);
    }

    VM::Frame::Frame(Stack &stack, Function *fn) : stack(stack), cont_fn(fn) {
        if (fn) stack.vm.mem.gc_add_ref(fn);
    }

    void VM::Frame::get_refs(const std::function<void(Allocation *)> &callback) {
        callback(cont_fn);
    }

    void VM::Frame::set_cont_fn(funscript::VM::Function *cont_fn1) {
        if (cont_fn) stack.vm.mem.gc_del_ref(cont_fn);
        cont_fn = cont_fn1;
        if (cont_fn) stack.vm.mem.gc_add_ref(cont_fn);
    }

    VM::Function *VM::Frame::get_cont_fn() const {
        return cont_fn;
    }

    VM::Frame::~Frame() {
        if (cont_fn) stack.vm.mem.gc_del_ref(cont_fn);
    }

    VM::Bytecode::Bytecode(std::string data) : bytes(std::move(data)) {}

    void VM::Bytecode::get_refs(const std::function<void(Allocation *)> &callback) {}

    void VM::BytecodeFunction::get_refs(const std::function<void(Allocation *)> &callback) {
        callback(bytecode);
        callback(scope);
    }

    void VM::BytecodeFunction::call(VM::Stack &stack, Frame *frame) {
        stack.exec_bytecode(scope, bytecode, offset, stack.find_sep());
    }

    VM::BytecodeFunction::BytecodeFunction(Scope *scope, Bytecode *bytecode, size_t offset) : scope(scope),
                                                                                              bytecode(bytecode),
                                                                                              offset(offset) {
        scope->vars->vm.mem.gc_add_ref(scope);
        scope->vars->vm.mem.gc_add_ref(bytecode);
    }

    VM::BytecodeFunction::~BytecodeFunction() {
        scope->vars->vm.mem.gc_del_ref(scope);
        scope->vars->vm.mem.gc_del_ref(bytecode);
    }

    VM::String::String(fstr bytes) : bytes(std::move(bytes)) {}

    void VM::String::get_refs(const std::function<void(Allocation *)> &callback) {}

    void VM::Error::get_refs(const std::function<void(Allocation *)> &callback) {}

    VM::Error::Error(fstr desc) : desc(std::move(desc)) {}

    void VM::Array::get_refs(const std::function<void(Allocation *)> &callback) {
        for (const auto &val : values) val.get_ref(callback);
    }

    VM::Array::Array(VM &vm, funscript::VM::Value *beg, size_t len) : vm(vm), values(len, Value{Type::NUL},
                                                                                     vm.mem.std_alloc<Value>()) {
        std::copy(beg, beg + len, values.data());
        for (const auto &val : values) val.get_ref([&vm](auto *ref) { vm.mem.gc_add_ref(ref); });
    }

    VM::Array::Array(funscript::VM &vm, size_t len) : vm(vm), values(len, {Type::NUL}, vm.mem.std_alloc<Value>()) {}

    const VM::Value &VM::Array::operator[](size_t pos) const {
        return values[pos];
    }

    const VM::Value *VM::Array::begin() const {
        return values.data();
    }

    const VM::Value *VM::Array::end() const {
        return values.data() + len();
    }

    size_t VM::Array::len() const {
        return values.size();
    }

    VM::Array::Array(VM &vm, const VM::Array *arr1, const VM::Array *arr2) : vm(vm), values(arr1->len() + arr2->len(),
                                                                                            Value{Type::NUL},
                                                                                            vm.mem.std_alloc<Value>()) {
        std::copy(arr1->values.data(), arr1->values.data() + arr1->len(), values.data());
        std::copy(arr2->values.data(), arr2->values.data() + arr2->len(), values.data() + arr1->len());
        for (const auto &val : values) val.get_ref([&vm](auto *ref) { vm.mem.gc_add_ref(ref); });
    }

    void VM::Array::set(size_t pos, const funscript::VM::Value &val) {
        values[pos].get_ref([this](auto *ref) { vm.mem.gc_del_ref(ref); });
        values[pos] = val;
        val.get_ref([this](auto *ref) { vm.mem.gc_add_ref(ref); });
    }

    VM::Array::~Array() {
        for (const auto &val : values) val.get_ref([this](auto *ref) { vm.mem.gc_del_ref(ref); });
    }
}