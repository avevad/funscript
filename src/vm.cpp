#include <queue>
#include <utility>
#include "vm.h"

namespace funscript {

    VM::MemoryManager::MemoryManager(VM &vm) : vm(vm), gc_tracked(std_alloc<std::pair<Allocation *const, size_t>>()),
                                               gc_pins((std_alloc<std::pair<Allocation *const, size_t>>())) {}

    void VM::MemoryManager::free(void *ptr) { vm.config.allocator->free(ptr); }

    void VM::MemoryManager::gc_track(VM::Allocation *alloc, size_t len) {
        if (gc_tracked.contains(alloc)) [[unlikely]] assertion_failed("allocation is already tracked");
        gc_tracked[alloc] = len;
        gc_pins[alloc] = 1;
    }

    void VM::MemoryManager::gc_pin(VM::Allocation *alloc) {
        if (!gc_tracked.contains(alloc)) [[unlikely]] assertion_failed("allocation is not tracked");
        gc_pins[alloc]++;
    }

    void VM::MemoryManager::gc_unpin(VM::Allocation *alloc) {
        if (!gc_tracked.contains(alloc)) [[unlikely]] assertion_failed("allocation is not tracked");
        if (!gc_pins[alloc]) assertion_failed("mismatched allocation unpin");
        gc_pins[alloc]--;
    }

    void VM::MemoryManager::gc_cycle() {
        std::queue<Allocation *, fdeq<Allocation *>> queue(std_alloc<Allocation *>()); // Allocations to be marked.
        // Populate the queue with GC roots
        auto unmarked = gc_tracked;
        for (const auto &[alloc, cnt] : gc_pins) {
            if (cnt == 0) continue;
            unmarked.erase(alloc);
            queue.push(alloc);
        }
        // Using BFS to mark all reachable allocations
        while (!queue.empty()) {
            auto *alloc = queue.front();
            queue.pop();
            size_t len = gc_tracked[alloc];
            for (size_t i = 0; i < len; i++) {
                alloc[i].get_refs([&queue, &unmarked](Allocation *ref) -> void {
                    if (!unmarked.contains(ref)) return;
                    unmarked.erase(ref);
                    queue.push(ref);
                });
            }
        }
        // Destroy unreachable allocations
        for (const auto &[alloc, len] : unmarked) {
            for (size_t i = 0; i < len; i++) alloc[i].~Allocation();
            free(alloc);
            gc_tracked.erase(alloc);
            gc_pins.erase(alloc);
        }
    }

    VM::MemoryManager::~MemoryManager() {
        for (const auto &[alloc, len] : gc_tracked) {
            if (gc_pins[alloc]) assertion_failed("destructing memory manager with pinned allocations");
            for (size_t i = 0; i < len; i++) alloc[i].~Allocation();
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

    const Value &VM::Stack::operator[](VM::Stack::pos_t pos) {
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

    void VM::Stack::push_err(funscript::Error *err) { push({Type::ERR, {.err = err}}); }

    bool VM::Stack::as_bln() {
        if (get(-1).type != Type::BLN || get(-2).type != Type::SEP) {
            assertion_failed("no implicit conversion to boolean");
        }
        return get(-1).data.bln;
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

    Value &VM::Stack::get(VM::Stack::pos_t pos) {
        if (pos < 0) pos += size();
        return values[pos];
    }

    void VM::Stack::exec_bytecode(funscript::Scope *scope, Bytecode *bytecode_obj, size_t offset, pos_t frame_start) {
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
                        push(scope->get_var(name));
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
                        push(obj->get_field(name));
                        vm.mem.gc_unpin(obj);
                    }
                    ip++;
                    break;
                }
                case Opcode::SET: {
                    fstr name(reinterpret_cast<const fstr::value_type *>(bytecode + ins.u64), vm.mem.str_alloc());
                    if (get(-1).type == Type::SEP) {
                        pop();
                        if (get(-1).type == Type::SEP) raise_err("not enough values to assign", frame_start);
                        scope->set_var(name, get(-1));
                        pop();
                    } else {
                        if (get(-1).type != Type::OBJ) raise_err("only objects are able to be indexed", frame_start);
                        Object *obj = get(-1).data.obj;
                        vm.mem.gc_pin(obj);
                        pop();
                        if (get(-1).type != Type::SEP) raise_err("can't index multiple values", frame_start);
                        pop();
                        if (get(-1).type == Type::SEP) raise_err("not enough values to assign", frame_start);
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
                    bool bln = as_bln();
                    discard();
                    if (!bln) ip = reinterpret_cast<const Instruction *>(bytecode + ins.u64);
                    else ip++;
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

    void Object::get_refs(const std::function<void(Allocation * )> &callback) {
        for (const auto &[key, val] : fields) val.get_ref(callback);
    }

    bool Object::contains_field(const fstr &key) const {
        return fields.contains(key);
    }

    Value Object::get_field(const fstr &key) const {
        if (!fields.contains(key)) assertion_failed("no such field");
        return fields.at(key);
    }

    void Object::set_field(const fstr &key, Value val) {
        fields[key] = val;
    }

    void Scope::get_refs(const std::function<void(Allocation * )> &callback) {
        callback(vars);
        callback(prev_scope);
    }

    void Scope::set_var(const funscript::fstr &name, funscript::Value val, Scope &first) {
        if (vars->contains_field(name)) vars->set_field(name, val);
        if (prev_scope) prev_scope->set_var(name, val, first);
        else first.vars->set_field(name, val);
    }

    Value Scope::get_var(const funscript::fstr &name) const {
        if (vars->contains_field(name)) return vars->get_field(name);
        if (prev_scope == nullptr) assertion_failed("no such variable");
        return prev_scope->get_var(name);
    }

    void Scope::set_var(const fstr &name, Value val) {
        set_var(name, val, *this);
    }

    Frame::Frame(Function *fn) : cont_fn(fn) {}

    void Frame::get_refs(const std::function<void(Allocation * )> &callback) {
        callback(cont_fn);
    }

    Bytecode::Bytecode(std::string data) : bytes(std::move(data)) {}

    void Bytecode::get_refs(const std::function<void(Allocation * )> &callback) {}

    void BytecodeFunction::get_refs(const std::function<void(Allocation * )> &callback) {
        callback(bytecode);
        callback(scope);
    }

    void BytecodeFunction::call(VM::Stack &stack, funscript::Frame *frame) {
        stack.exec_bytecode(scope, bytecode, offset, stack.find_sep());
    }

    BytecodeFunction::BytecodeFunction(Scope *scope, Bytecode *bytecode, size_t offset) : scope(scope),
                                                                                          bytecode(bytecode),
                                                                                          offset(offset) {}

    String::String(fstr bytes) : bytes(std::move(bytes)) {}

    void String::get_refs(const std::function<void(Allocation * )> &callback) {}

    void Error::get_refs(const std::function<void(Allocation * )> &callback) {}

    Error::Error(fstr desc) : desc(std::move(desc)) {}
}