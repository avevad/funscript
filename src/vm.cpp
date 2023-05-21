#include <queue>
#include <utility>
#include "vm.h"

namespace funscript {

    VM::MemoryManager::MemoryManager(VM &vm) : vm(vm), gc_tracked(std_alloc<Allocation *>()) {}

    void VM::MemoryManager::free(void *ptr) { vm.config.allocator->free(ptr); }

    void VM::MemoryManager::gc_pin(VM::Allocation *alloc) {
        if (alloc->mm != this) [[unlikely]] assertion_failed("allocation is not tracked");
        alloc->gc_pins++;
    }

    void VM::MemoryManager::gc_unpin(VM::Allocation *alloc) {
        if (alloc->mm != this) [[unlikely]] assertion_failed("allocation is not tracked");
        if (!alloc->gc_pins) [[unlikely]] assertion_failed("mismatched allocation unpin");
        alloc->gc_pins--;
    }

    void VM::MemoryManager::gc_cycle() {
        std::queue<Allocation *, fdeq<Allocation *>> queue(std_alloc<Allocation *>()); // Allocations to be marked.
        // Populate the queue with GC roots, unmark other allocations
        for (auto *alloc : gc_tracked) {
            if (alloc->gc_pins) queue.push(alloc);
            else alloc->mm = nullptr;
        }
        // Using BFS to mark all reachable allocations
        while (!queue.empty()) {
            auto *alloc = queue.front();
            queue.pop();
            alloc->get_refs([&queue, this](Allocation *ref) -> void {
                if (!ref || ref->mm) return;
                ref->mm = this;
                queue.push(ref);
            });
        }
        fvec<Allocation *> gc_tracked_new(std_alloc<Allocation *>());
        // Remove track of unreachable allocations and destroy them
        for (auto *alloc : gc_tracked) {
            if (alloc->mm) gc_tracked_new.push_back(alloc);
            else {
                alloc->~Allocation();
                free(alloc);
            }
        }
        gc_tracked = gc_tracked_new;
    }

    VM::MemoryManager::~MemoryManager() {
        for (auto *alloc : gc_tracked) {
            if (alloc->gc_pins) assertion_failed("destructing memory manager with pinned allocations");
            alloc->~Allocation();
            free(alloc);
        }
    }


    VM::VM(VM::Config config) : config(config), mem(*this) {

    }

    void VM::Stack::get_refs(const std::function<void(Allocation *)> &callback) {
        for (const auto &val : values) val.get_ref(callback);
        for (auto *frame : frames) callback(frame);
    }

    VM::Stack::Stack(VM &vm, Function *start) : vm(vm), values(vm.mem.std_alloc<Value>()),
                                                frames(vm.mem.std_alloc<Frame *>()) {
        frames.push_back(vm.mem.gc_new<Frame>(start));
        vm.mem.gc_unpin(frames.back());
        push_sep(); // Empty arguments for the start function.
    }

    VM::Stack::pos_t VM::Stack::size() const {
        return values.size(); // NOLINT(cppcoreguidelines-narrowing-conversions)
    }

    const VM::Value &VM::Stack::operator[](VM::Stack::pos_t pos) {
        if (pos < 0) pos += size();
        return values[pos];
    }

    void VM::Stack::push_sep() { push({Type::SEP}); }

    void VM::Stack::push_nul() { push({Type::NUL}); }

    void VM::Stack::push_int(fint num) { push({Type::INT, num}); }

    void VM::Stack::push_obj(Object *obj) { push({Type::OBJ, {.obj = obj}}); }

    void VM::Stack::push_fun(Function *fun) { push({Type::FUN, {.fun = fun}}); }

    void VM::Stack::push_bln(bool bln) { push({Type::BLN, {.bln = bln}}); }

    void VM::Stack::push_str(String *str) { push({Type::STR, {.str = str}}); }

    void VM::Stack::push_err(Error *err) { push({Type::ERR, {.err = err}}); }

    void VM::Stack::push_arr(VM::Array *arr) { push({Type::ARR, {.arr = arr}}); }

    void VM::Stack::as_boolean() {
        if (get(-1).type != Type::BLN || get(-2).type != Type::SEP) {
            return raise_err("no implicit conversion to boolean", find_sep());
        }
        bool bln = get(-1).data.bln;
        pop(find_sep());
        push_bln(bln);
    }

    void VM::Stack::discard() { pop(find_sep()); }

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

    void VM::Stack::push(const Value &e) { values.push_back(e); }

    VM::Value &VM::Stack::get(VM::Stack::pos_t pos) {
        if (pos < 0) pos += size();
        return values[pos];
    }

    void VM::Stack::exec_bytecode(Scope *scope, Bytecode *bytecode_obj, size_t offset, pos_t frame_start) {
        const auto *bytecode = bytecode_obj->bytes.data();
        const auto *ip = reinterpret_cast<const Instruction *>(bytecode + offset);
        while (true) {
            Instruction ins = *ip;
            switch (ins.op) {
                case Opcode::NOP:
                    ip++;
                    break;
                case Opcode::VAL: {
                    Value val{.type = static_cast<Type>(ins.u16), .data{.num = static_cast<fint>(ins.u64)}};
                    if (val.type == Type::OBJ) val.data.obj = scope->vars;
                    if (val.type == Type::FUN) {
                        auto *fun = vm.mem.gc_new<BytecodeFunction>(scope, bytecode_obj, size_t(ins.u64));
                        val.data.fun = fun;
                    }
                    push(val);
                    if (val.type == Type::FUN) {
                        vm.mem.gc_unpin(val.data.fun);
                    }
                    ip++;
                    break;
                }
                case Opcode::SEP: {
                    push_sep();
                    ip++;
                    break;
                }
                case Opcode::GET: {
                    fstr name(reinterpret_cast<const fstr::value_type *>(bytecode + ins.u64), vm.mem.str_alloc());
                    if (get(-1).type == Type::SEP) {
                        pop();
                        auto var = scope->get_var(name);
                        if (!var.has_value()) {
                            return raise_err("no such variable: '" + std::string(name) + "'", frame_start);
                        }
                        push(var.value());
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
                        push(field.value());
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
                        scope->set_var(name, get(-1));
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
                        auto *vars = vm.mem.gc_new<Object>(vm);
                        scope = vm.mem.gc_new<Scope>(vars, scope);
                        vm.mem.gc_unpin(vars);
                    } else {
                        Scope *prev = scope->prev_scope;
                        vm.mem.gc_unpin(scope);
                        scope = prev;
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
                        push_err(err);
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
                        push_err(err);
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
                    auto *str_obj = vm.mem.gc_new<String>(str);
                    push_str(str_obj);
                    vm.mem.gc_unpin(str_obj);
                    ip++;
                    break;
                }
                case Opcode::ARR: {
                    pos_t beg = find_sep() + 1;
                    auto *arr = vm.mem.gc_new<Array>(vm, values.data() + beg, size() - beg);
                    pop(beg - 1);
                    ip++;
                    push_arr(arr);
                    vm.mem.gc_unpin(arr);
                    break;
                }
                case Opcode::MOV: {
                    call_assignment(nullptr);
                    if (get(-1).type == Type::ERR) {
                        Error *err = get(-1).data.err;
                        vm.mem.gc_pin(err);
                        pop(frame_start);
                        push_err(err);
                        vm.mem.gc_unpin(err);
                        return;
                    }
                    ip++;
                    break;
                }
            }
        }
    }

    void VM::Stack::call_operator(Operator op, Function *cont_fn) {
        // Calculate stack positions of operands and their lengths
        pos_t pos_a = find_sep() + 1, pos_b = find_sep(pos_a - 1) + 1;
        pos_t cnt_a = size() - pos_a, cnt_b = pos_a - pos_b - 1;
        switch (op) {
            case Operator::TIMES: {
                if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                    return raise_op_err(op);
                }
                fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                pop(-4);
                push_int(a * b);
                break;
            }
            case Operator::DIVIDE: {
                if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                    return raise_op_err(op);
                }
                fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                if (b == 0) return raise_err("division by zero", -4);
                pop(-4);
                push_int(a / b);
                break;
            }
            case Operator::PLUS: {
                if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::STR && get(pos_b).type == Type::STR) {
                    fstr a = get(pos_a).data.str->bytes, b = get(pos_b).data.str->bytes;
                    pop(-4);
                    auto *str = vm.mem.gc_new<String>(a + b);
                    push_str(str);
                    vm.mem.gc_unpin(str);
                    break;
                }
                if (cnt_a == 1 && cnt_b == 1 && get(pos_a).type == Type::ARR && get(pos_b).type == Type::ARR) {
                    size_t a_len = get(pos_a).data.arr->len();
                    Value *a_dat = get(pos_a).data.arr->begin();
                    size_t b_len = get(pos_b).data.arr->len();
                    Value *b_dat = get(pos_b).data.arr->begin();
                    auto *arr = vm.mem.gc_new<Array>(vm, a_len + b_len);
                    std::copy(a_dat, a_dat + a_len, arr->begin());
                    std::copy(b_dat, b_dat + b_len, arr->begin() + a_len);
                    pop(-4);
                    push_arr(arr);
                    vm.mem.gc_unpin(arr);
                    break;
                }
                if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                    return raise_op_err(op);
                }
                fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                pop(-4);
                push_int(a + b);
                break;
            }
            case Operator::MINUS: {
                if (cnt_a == 0) {
                    if (cnt_b != 1 || get(pos_b).type != Type::INT) {
                        return raise_op_err(op);
                    }
                    fint num = get(pos_b).data.num;
                    pop(-3);
                    push_int(-num);
                    break;
                }
                if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                    return raise_op_err(op);
                }
                fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                pop(-4);
                push_int(a - b);
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
                        push(arr[val.data.num]);
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
                push_int(a % b);
                break;
            }
            case Operator::EQUALS: {
                if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                    return raise_op_err(op);
                }
                fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                pop(-4);
                push_bln(a == b);
                break;
            }
            case Operator::DIFFERS: {
                if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                    return raise_op_err(op);
                }
                fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                pop(-4);
                push_bln(a != b);
                break;
            }
            case Operator::NOT: {
                if (cnt_a != 0 || cnt_b != 1 || get(pos_b).type != Type::INT) {
                    return raise_op_err(op);
                }
                bool bln = get(pos_b).data.bln;
                pop(-3);
                push_bln(!bln);
                break;
            }
            case Operator::LESS: {
                if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                    return raise_op_err(op);
                }
                fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                pop(-4);
                push_bln(a < b);
                break;
            }
            case Operator::GREATER: {
                if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                    return raise_op_err(op);
                }
                fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                pop(-4);
                push_bln(a > b);
                break;
            }
            case Operator::LESS_EQUAL: {
                if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                    return raise_op_err(op);
                }
                fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                pop(-4);
                push_bln(a <= b);
                break;
            }
            case Operator::GREATER_EQUAL: {
                if (cnt_a != 1 || cnt_b != 1 || get(pos_a).type != Type::INT || get(pos_b).type != Type::INT) {
                    return raise_op_err(op);
                }
                fint a = get(pos_a).data.num, b = get(pos_b).data.num;
                pop(-4);
                push_bln(a >= b);
                break;
            }
            default:
                assertion_failed("unknown operator");
        }
    }

    void VM::Stack::call_assignment(funscript::VM::Function *cont_fn) {
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
                    return raise_err("invalid array index", beg);
                }
                if (get(-1).type == Type::SEP) {
                    vm.mem.gc_unpin(&arr);
                    vm.mem.gc_unpin(&ind);
                    return raise_err("not enough values to assign", beg);
                }
                arr[val.data.num] = get(-1);
                pop();
            }
            vm.mem.gc_unpin(&arr);
            vm.mem.gc_unpin(&ind);
            return;
        }
        return raise_op_err(Operator::CALL);
    }

    void VM::Stack::call_function(Function *fun, Function *cont_fn) {
        frames.back()->cont_fn = cont_fn;
        auto *frame = vm.mem.gc_new<Frame>(nullptr);
        frames.push_back(frame);
        vm.mem.gc_unpin(frame);
        fun->call(*this, frame);
        frames.pop_back();
        frames.back()->cont_fn = nullptr;
    }

    void VM::Stack::continue_execution() {
        if (frames.empty()) assertion_failed("the routine is no longer alive");
        for (pos_t pos = pos_t(frames.size()) - 1; pos >= 0; pos--) {
            Function *cont_fn = frames[pos]->cont_fn;
            vm.mem.gc_pin(cont_fn);
            frames[pos]->cont_fn = nullptr;
            cont_fn->call(*this, frames[pos]);
            vm.mem.gc_unpin(cont_fn);
        }
    }

    void VM::Stack::raise_err(const std::string &msg, VM::Stack::pos_t frame_start) {
        pop(frame_start);
        auto *err = vm.mem.gc_new<Error>(fstr(msg, vm.mem.std_alloc<char>()));
        push_err(err);
        vm.mem.gc_unpin(err);
    }

    void VM::Stack::raise_op_err(Operator op) {
        pos_t pos = find_sep(find_sep());
        raise_err("operator is not defined for these operands", pos);
    }

    VM::Stack::~Stack() = default;

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
        fields[key] = val;
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

    VM::Frame::Frame(Function *fn) : cont_fn(fn) {}

    void VM::Frame::get_refs(const std::function<void(Allocation *)> &callback) {
        callback(cont_fn);
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
                                                                                              offset(offset) {}

    VM::String::String(fstr bytes) : bytes(std::move(bytes)) {}

    void VM::String::get_refs(const std::function<void(Allocation *)> &callback) {}

    void VM::Error::get_refs(const std::function<void(Allocation *)> &callback) {}

    VM::Error::Error(fstr desc) : desc(std::move(desc)) {}

    void VM::Array::get_refs(const std::function<void(Allocation *)> &callback) {
        for (const auto &val : values) val.get_ref(callback);
    }

    VM::Array::Array(VM &vm, funscript::VM::Value *beg, size_t len) : values(len, Value{Type::NUL},
                                                                             vm.mem.std_alloc<Value>()) {
        std::copy(beg, beg + len, values.data());
    }

    VM::Array::Array(funscript::VM &vm, size_t len) : values(len, {Type::NUL}, vm.mem.std_alloc<Value>()) {}

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
}