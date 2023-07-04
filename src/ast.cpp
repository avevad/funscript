#include "ast.hpp"

#include <utility>

namespace funscript {

    AST::AST(std::string filename, code_loc_t location) : filename(std::move(filename)), token_loc(location) {}

    code_loc_t AST::get_location() const {
        return token_loc;
    }

    u_ev_opt_info IntegerAST::compile_eval(Assembler &as, Assembler::Chunk &ch, const d_ev_opt_info &d_opt) {
        ch.put_instruction({Opcode::VAL, uint32_t(as.data_chunk().put(token_loc.beg)),
                            static_cast<uint16_t>(Type::INT), static_cast<uint64_t>(num)});
        return {.no_scope = true};
    }

    u_mv_opt_info IntegerAST::compile_move(Assembler &as, Assembler::Chunk &ch, const d_mv_opt_info &d_opt) {
        throw CompilationError(filename, get_location(), "expression is not assignable");
    }

    IntegerAST::IntegerAST(const std::string &filename, code_loc_t token_loc, int64_t num) : AST(filename, token_loc),
                                                                                             num(num) {}

    u_ev_opt_info IdentifierAST::compile_eval(Assembler &as, Assembler::Chunk &ch, const d_ev_opt_info &d_opt) {
        ch.put_instruction({Opcode::VGT, uint32_t(as.data_chunk().put(token_loc.beg)),
                            false, 0 /* Will be overwritten to actual name location */});
        as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), 0, as.add_string(name));
        return {.no_scope = true};
    }

    u_mv_opt_info IdentifierAST::compile_move(Assembler &as, Assembler::Chunk &ch, const d_mv_opt_info &d_opt) {
        ch.put_instruction({Opcode::VST, uint32_t(as.data_chunk().put(token_loc.beg)),
                            false, 0 /* Will be overwritten to actual name location */});
        as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), 0, as.add_string(name));
        return {.no_scope = true};
    }

    IdentifierAST::IdentifierAST(const std::string &filename, code_loc_t token_loc,
                                 std::string name) : AST(filename, token_loc), name(std::move(name)) {}

    u_ev_opt_info OperatorAST::compile_eval(Assembler &as, Assembler::Chunk &ch, const d_ev_opt_info &d_opt) {
        switch (op) {
            case Operator::ASSIGN: {
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(right->get_location().beg)),
                                    0, 0});
                u_ev_opt_info u_opt1 = right->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::REV, uint32_t(as.data_chunk().put(token_loc.beg)), 0, 0});
                u_mv_opt_info u_opt2 = left->compile_move(as, ch, {});
                ch.put_instruction({Opcode::DIS, uint32_t(as.data_chunk().put(token_loc.beg)), true, 0});
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            case Operator::APPEND: {
                u_ev_opt_info u_opt1 = left->compile_eval(as, ch, {});
                u_ev_opt_info u_opt2 = right->compile_eval(as, ch, {});
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            case Operator::DISCARD: {
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(left->get_location().beg)),
                                    0, 0});
                u_ev_opt_info u_opt1 = left->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::DIS, uint32_t(as.data_chunk().put(token_loc.beg)), 0, 0});
                u_ev_opt_info u_opt2 = right->compile_eval(as, ch, {});
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            case Operator::LAMBDA: {
                auto &new_ch = as.new_chunk(); // Chunk of the new function
                ch.put_instruction({Opcode::VAL, uint32_t(as.data_chunk().put(token_loc.beg)),
                                    static_cast<uint16_t>(Type::FUN), 0});
                as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), new_ch.id, 0);
                // Here goes the bytecode of new function
                new_ch.put_instruction({Opcode::MET, 0, 0, 0 /* Will be overwritten to actual DATA chunk location */});
                as.add_pointer(new_ch.id, new_ch.size() - sizeof(Instruction::u64), as.data_chunk().id, 0);
                new_ch.put_instruction({Opcode::SCP, uint32_t(as.data_chunk().put(token_loc.beg)),
                                        true, 0}); // Create the scope of the function
                new_ch.put_instruction({Opcode::REV, uint32_t(as.data_chunk().put(token_loc.beg)),
                                        0, 0}); // Prepare arguments for assignment
                u_mv_opt_info u_opt1 = left->compile_move(as, new_ch, {}); // Assign function arguments
                new_ch.put_instruction({Opcode::DIS, uint32_t(as.data_chunk().put(token_loc.beg)),
                                        true, 0}); // Discard the separator after the arguments
                u_ev_opt_info u_opt2 = right->compile_eval(as, new_ch, {}); // Evaluate function body
                new_ch.put_instruction({Opcode::SCP, uint32_t(as.data_chunk().put(token_loc.beg)),
                                        false, 0}); // Discard the scope ot the function
                new_ch.put_instruction({Opcode::END, uint32_t(as.data_chunk().put(right->get_location().end)),
                                        0, 0});
                return {.no_scope = true};
            }
            case Operator::INDEX: {
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(left->get_location().beg)),
                                    0, 0});
                u_ev_opt_info u_opt0 = left->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::GET, uint32_t(as.data_chunk().put(token_loc.beg)),
                                    false, 0 /* Will be overwritten to actual name location */});
                auto *right_id = dynamic_cast<IdentifierAST *>(right.get());
                if (!right_id) {
                    throw CompilationError(filename, right->get_location(), "identifier expected");
                }
                as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), 0, as.add_string(right_id->name));
                return {.no_scope = false};
            }
            case Operator::THEN: {
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(left->get_location().beg)),
                                    0, 0});
                u_ev_opt_info u_opt1 = left->compile_eval(as, ch, {});
                auto pos = ch.put_instruction(); // Position of jump instruction (over `then` expression)
                u_ev_opt_info u_opt2 = right->compile_eval(as, ch, {});
                ch.set_instruction(pos, {Opcode::JNO, uint32_t(as.data_chunk().put(token_loc.beg)),
                                         0, 0});
                as.add_pointer(ch.id, pos + sizeof(Instruction) - sizeof(Instruction::u64), ch.id, ch.size());
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            case Operator::ELSE: {
                auto left_op = dynamic_cast<OperatorAST *>(left.get());
                if (!left_op || left_op->op != Operator::THEN) {
                    throw CompilationError(filename, left->get_location(), "expected `then` operator");
                }
                auto cond = left_op->left.get(), then = left_op->right.get();
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(cond->get_location().beg)),
                                    0, 0});
                u_ev_opt_info u_opt1 = cond->compile_eval(as, ch, {});
                auto pos1 = ch.put_instruction(); // Position of jump instruction (over `then` expression)
                u_ev_opt_info u_opt2 = then->compile_eval(as, ch, {});
                auto pos2 = ch.put_instruction(); // Position of jump instruction (over `else` expression)
                ch.set_instruction(pos1, {Opcode::JNO, uint32_t(as.data_chunk().put(left->token_loc.beg)),
                                          0, 0});
                as.add_pointer(ch.id, pos1 + sizeof(Instruction) - sizeof(Instruction::u64), ch.id, ch.size());
                u_ev_opt_info u_opt3 = right->compile_eval(as, ch, {});
                ch.set_instruction(pos2, {Opcode::JMP, uint32_t(as.data_chunk().put(token_loc.beg)),
                                          0, 0});
                as.add_pointer(ch.id, pos2 + sizeof(Instruction) - sizeof(Instruction::u64), ch.id, ch.size());
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope && u_opt3.no_scope};
            }
            case Operator::UNTIL: {
                auto pos = ch.size(); // Position of where to jump back
                u_ev_opt_info u_opt1 = left->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(right->get_location().beg)), 0, 0});
                u_ev_opt_info u_opt2 = right->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::JNO, uint32_t(as.data_chunk().put(right->get_location().end)), 0, 0});
                as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), ch.id, pos);
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            case Operator::REPEATS: {
                auto pos0 = ch.size(); // Position of where to jump back
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(token_loc.beg)), 0, 0});
                u_ev_opt_info u_opt1 = left->compile_eval(as, ch, {});
                auto pos1 = ch.put_instruction(); // Position of jump instruction (over the body of the loop)
                u_ev_opt_info u_opt2 = right->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::JMP, uint32_t(as.data_chunk().put(right->get_location().end)),
                                    0, 0});
                as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), ch.id, pos0);
                ch.set_instruction(pos1, {Opcode::JNO, uint32_t(as.data_chunk().put(token_loc.beg)),
                                          0, 0});
                as.add_pointer(ch.id, pos1 + sizeof(Instruction) - sizeof(Instruction::u64), ch.id, ch.size());
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            case Operator::AND: {
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(left->get_location().beg)),
                                    0, 0});
                u_ev_opt_info u_opt1 = left->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::DUP, uint32_t(as.data_chunk().put(left->get_location().end)),
                                    0, 0}); // Preserve the value before implicitly converting it to boolean
                auto pos = ch.put_instruction(); // Position of jump instruction (over the right operand)
                ch.put_instruction({Opcode::DIS, uint32_t(as.data_chunk().put(left->get_location().end)),
                                    0, 0});
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(right->get_location().beg)),
                                    0, 0});
                u_ev_opt_info u_opt2 = right->compile_eval(as, ch, {});
                ch.set_instruction(pos, {Opcode::JNO, uint32_t(as.data_chunk().put(token_loc.beg)),
                                         0, 0});
                as.add_pointer(ch.id, pos + sizeof(Instruction) - sizeof(Instruction::u64), ch.id, ch.size());
                ch.put_instruction({Opcode::REM, uint32_t(as.data_chunk().put(get_location().end)),
                                    0, 0});
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            case Operator::OR: {
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(left->get_location().beg)),
                                    0, 0});
                u_ev_opt_info u_opt1 = left->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::DUP, uint32_t(as.data_chunk().put(left->get_location().end)),
                                    0, 0}); // Preserve the value before implicitly converting it to boolean
                auto pos = ch.put_instruction(); // Position of jump instruction (over the right operand)
                ch.put_instruction({Opcode::DIS, uint32_t(as.data_chunk().put(left->get_location().end)),
                                    0, 0});
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(right->get_location().beg)),
                                    0, 0});
                u_ev_opt_info u_opt2 = right->compile_eval(as, ch, {});
                ch.set_instruction(pos, {Opcode::JYS, uint32_t(as.data_chunk().put(token_loc.beg)),
                                         0, 0});
                as.add_pointer(ch.id, pos + sizeof(Instruction) - sizeof(Instruction::u64), ch.id, ch.size());
                ch.put_instruction({Opcode::REM, uint32_t(as.data_chunk().put(get_location().beg)),
                                    0, 0});
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            case Operator::EXTRACT: {
                if (dynamic_cast<VoidAST *>(right.get())) { // As in `.file = io.open(path)?`
                    ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(left->get_location().beg)),
                                        0, 0});
                    auto u_opt = left->compile_eval(as, ch, {});
                    ch.put_instruction({Opcode::EXT, uint32_t(as.data_chunk().put(token_loc.beg)),
                                        0, 0});
                    return {.no_scope = u_opt.no_scope};
                } else { // As in `.name = next_string() ? 'Unnamed'`
                    ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(left->get_location().beg)),
                                        0, 0});
                    auto u_opt1 = left->compile_eval(as, ch, {});
                    auto pos = ch.put_instruction(); // Position of extract-and-jump instruction (over the fallback expression)
                    auto u_opt2 = right->compile_eval(as, ch, {});
                    ch.set_instruction(pos, {Opcode::EXT, uint32_t(as.data_chunk().put(token_loc.beg)),
                                             0, 0});
                    as.add_pointer(ch.id, pos + sizeof(Instruction) - sizeof(Instruction::u64), ch.id, ch.size());
                    return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
                }
            }
            case Operator::CHECK: {
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(left->get_location().beg)),
                                    0, 0});
                u_ev_opt_info u_opt1 = right->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(right->get_location().beg)),
                                    0, 0});
                u_ev_opt_info u_opt2 = left->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::CHK, uint32_t(as.data_chunk().put(token_loc.beg)),
                                    false, 0});
                ch.put_instruction({Opcode::REM, uint32_t(as.data_chunk().put(token_loc.beg)),
                                    0, 0});
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            default: {
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(right->get_location().beg)),
                                    0, 0});
                u_ev_opt_info u_opt1 = right->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(left->get_location().beg)),
                                    0, 0});
                u_ev_opt_info u_opt2 = left->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::OPR, uint32_t(as.data_chunk().put(token_loc.beg)),
                                    static_cast<uint16_t>(op), 0});
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
        }
    }

    u_mv_opt_info OperatorAST::compile_move(Assembler &as, Assembler::Chunk &ch, const d_mv_opt_info &d_opt) {
        switch (op) {
            case Operator::APPEND: {
                u_mv_opt_info u_opt1 = left->compile_move(as, ch, {});
                u_mv_opt_info u_opt2 = right->compile_move(as, ch, {});
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            case Operator::INDEX: {
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(left->get_location().beg)), 0, 0});
                u_ev_opt_info u_opt0 = left->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::SET, uint32_t(as.data_chunk().put(token_loc.beg)),
                                    false, 0 /* Will be overwritten to actual name location */});
                auto *right_id = dynamic_cast<IdentifierAST *>(right.get());
                if (!right_id) {
                    throw CompilationError(filename, left->get_location(), "identifier expected");
                }
                as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), 0, as.add_string(right_id->name));
                return {.no_scope = false};
            }
            case Operator::CALL: {
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(right->get_location().beg)),
                                    0, 0});
                u_ev_opt_info u_opt1 = right->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(left->get_location().beg)),
                                    0, 0});
                u_ev_opt_info u_opt2 = left->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::MOV, uint32_t(as.data_chunk().put(token_loc.beg)),
                                    0, 0});
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            case Operator::CHECK: {
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(right->get_location().beg)),
                                    0, 0});
                u_ev_opt_info u_opt1 = right->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::REV, uint32_t(as.data_chunk().put(token_loc.beg)),
                                    0, 0});
                ch.put_instruction({Opcode::CHK, uint32_t(as.data_chunk().put(token_loc.beg)),
                                    true, 0});
                u_mv_opt_info u_opt2 = left->compile_move(as, ch, {});
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            default:
                throw CompilationError(filename, get_location(), "expression is not assignable");
        }
    }

    OperatorAST::OperatorAST(const std::string &filename, code_loc_t token_loc,
                             AST *left, AST *right, funscript::Operator op) : AST(filename, token_loc),
                                                                              left(left),
                                                                              right(right),
                                                                              op(op) {}

    code_loc_t OperatorAST::get_location() const {
        return {left->get_location().beg, right->get_location().end};
    }

    u_ev_opt_info VoidAST::compile_eval(Assembler &as, Assembler::Chunk &ch, const d_ev_opt_info &d_opt) {
        return {.no_scope = true};
    }

    u_mv_opt_info VoidAST::compile_move(Assembler &as, Assembler::Chunk &ch, const d_mv_opt_info &d_opt) {
        return {.no_scope = true};
    }

    VoidAST::VoidAST(const std::string &filename, code_loc_t token_loc) : AST(filename, token_loc) {}

    u_ev_opt_info BracketAST::compile_eval(Assembler &as, Assembler::Chunk &ch, const d_ev_opt_info &d_opt) {
        u_ev_opt_info u_opt0;
        switch (type) {
            case Bracket::PLAIN: {
                size_t scp_pos = ch.put_instruction({Opcode::SCP, uint32_t(as.data_chunk().put(token_loc.beg)),
                                                     true, 0});
                u_opt0 = child->compile_eval(as, ch, {});
                ch.put_instruction(
                        {u_opt0.no_scope ? Opcode::NOP : Opcode::SCP, uint32_t(as.data_chunk().put(token_loc.end)),
                         false, 0});
                if (u_opt0.no_scope) {
                    ch.set_instruction(scp_pos, {Opcode::NOP, uint32_t(as.data_chunk().put(token_loc.beg)),
                                                 0, 0});
                }
                break;
            }
            case Bracket::CURLY: {
                ch.put_instruction({Opcode::SCP, uint32_t(as.data_chunk().put(token_loc.beg)),
                                    true, 0}); // Create object scope
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(child->get_location().beg)),
                                    0, 0});
                u_opt0 = child->compile_eval(as, ch, {});
                ch.put_instruction(
                        {Opcode::OBJ, uint32_t(as.data_chunk().put(token_loc.end)),
                         0, 0}); // Create an object from scope
                ch.put_instruction(
                        {Opcode::SCP, uint32_t(as.data_chunk().put(token_loc.end)),
                         false, 0}); // Discard object scope
                break;
            }
            case Bracket::SQUARE: {
                size_t scp_pos = ch.put_instruction({Opcode::SCP, uint32_t(as.data_chunk().put(token_loc.beg)),
                                                     true, 0});
                ch.put_instruction({Opcode::SEP, uint32_t(as.data_chunk().put(child->get_location().beg)),
                                    0, 0});
                u_opt0 = child->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::ARR, uint32_t(as.data_chunk().put(token_loc.end)),
                                    0, 0});
                ch.put_instruction(
                        {u_opt0.no_scope ? Opcode::NOP : Opcode::SCP, uint32_t(as.data_chunk().put(token_loc.end)),
                         false, 0});
                if (u_opt0.no_scope) {
                    ch.set_instruction(scp_pos, {Opcode::NOP, uint32_t(as.data_chunk().put(token_loc.beg)),
                                                 0, 0});
                }
                break;
            }
        }
        return {.no_scope = true};
    }

    u_mv_opt_info BracketAST::compile_move(Assembler &as, Assembler::Chunk &ch, const d_mv_opt_info &d_opt) {
        u_mv_opt_info u_opt0;
        switch (type) {
            case Bracket::PLAIN:
                u_opt0 = child->compile_move(as, ch, {});
                break;
            case Bracket::CURLY:
            case Bracket::SQUARE:
                throw CompilationError(filename, get_location(), "expression is not assignable");
        }
        return {.no_scope = u_opt0.no_scope};
    }

    BracketAST::BracketAST(const std::string &filename, code_loc_t token_loc,
                           funscript::AST *child, funscript::Bracket type) : AST(filename, token_loc), type(type),
                                                                             child(child) {}

    u_ev_opt_info BooleanAST::compile_eval(Assembler &as, Assembler::Chunk &ch, const d_ev_opt_info &d_opt) {
        ch.put_instruction({Opcode::VAL, uint32_t(as.data_chunk().put(token_loc.beg)),
                            static_cast<uint16_t>(Type::BLN), static_cast<uint64_t>(bln)});
        return {.no_scope = true};
    }

    u_mv_opt_info BooleanAST::compile_move(Assembler &as, Assembler::Chunk &ch, const d_mv_opt_info &d_opt) {
        throw CompilationError(filename, get_location(), "expression is not assignable");
    }

    BooleanAST::BooleanAST(const std::string &filename, code_loc_t token_loc, bool bln) : AST(filename, token_loc),
                                                                                          bln(bln) {}

    StringAST::StringAST(const std::string &filename, code_loc_t token_loc,
                         std::string str) : AST(filename, token_loc), str(std::move(str)) {}

    u_ev_opt_info StringAST::compile_eval(Assembler &as, Assembler::Chunk &ch, const d_ev_opt_info &d_opt) {
        ch.put_instruction({Opcode::STR, uint32_t(as.data_chunk().put(token_loc.beg)),
                            static_cast<uint16_t>(str.size()), 0 /* Will be overwritten to actual string location */});
        as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), 0, as.add_string(str));
        return {.no_scope = true};
    }

    u_mv_opt_info StringAST::compile_move(Assembler &as, Assembler::Chunk &chunk, const d_mv_opt_info &d_opt) {
        throw CompilationError(filename, get_location(), "expression is not assignable");
    }

    FloatAST::FloatAST(const std::string &filename, code_loc_t token_loc,
                       double flp) : AST(filename, token_loc), flp(flp) {}

    u_ev_opt_info FloatAST::compile_eval(Assembler &as, Assembler::Chunk &ch, const d_ev_opt_info &d_opt) {
        ch.put_instruction({Opcode::VAL, uint32_t(as.data_chunk().put(token_loc.beg)),
                            static_cast<uint16_t>(Type::FLP), *reinterpret_cast<uint64_t *>(&flp)});
        return {.no_scope = true};
    }

    u_mv_opt_info FloatAST::compile_move(Assembler &as, Assembler::Chunk &ch, const d_mv_opt_info &d_opt) {
        throw CompilationError(filename, get_location(), "expression is not assignable");
    }

}