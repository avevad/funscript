#include "../include/ast.h"

namespace funscript {

    std::string AST::get_identifier() const {
        throw CompilationError("not an identifier");
    }

    std::pair<AST *, AST *> AST::get_then() const {
        throw CompilationError("not a `then` operator");
    }

    AST::AST() = default;

    u_ev_opt_info IntegerAST::compile_eval(Assembler &as, Assembler::Chunk &ch, const d_ev_opt_info &d_opt) {
        ch.put_instruction({Opcode::VAL, static_cast<uint16_t>(Type::INT), static_cast<uint64_t>(num)});
        return {.no_scope = true};
    }

    u_mv_opt_info IntegerAST::compile_move(Assembler &as, Assembler::Chunk &ch, const d_mv_opt_info &d_opt) {
        throw CompilationError("expression is not assignable");
    }

    IntegerAST::IntegerAST(int64_t num) : num(num) {}

    u_ev_opt_info IdentifierAST::compile_eval(Assembler &as, Assembler::Chunk &ch, const d_ev_opt_info &d_opt) {
        ch.put_instruction({Opcode::VGT, false, 0 /* Will be overwritten to actual name location */});
        as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), 0, as.add_string(name));
        return {.no_scope = true};
    }

    u_mv_opt_info IdentifierAST::compile_move(Assembler &as, Assembler::Chunk &ch, const d_mv_opt_info &d_opt) {
        ch.put_instruction({Opcode::VST, false, 0 /* Will be overwritten to actual name location */});
        as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), 0, as.add_string(name));
        return {.no_scope = true};
    }

    IdentifierAST::IdentifierAST(std::string name) : name(std::move(name)) {}

    std::string IdentifierAST::get_identifier() const {
        return name;
    }

    u_ev_opt_info OperatorAST::compile_eval(Assembler &as, Assembler::Chunk &ch, const d_ev_opt_info &d_opt) {
        switch (op) {
            case Operator::ASSIGN: {
                ch.put_instruction(Opcode::SEP);
                u_ev_opt_info u_opt1 = right->compile_eval(as, ch, {});
                ch.put_instruction(Opcode::REV);
                u_mv_opt_info u_opt2 = left->compile_move(as, ch, {});
                ch.put_instruction({Opcode::DIS, true});
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            case Operator::APPEND: {
                u_ev_opt_info u_opt1 = left->compile_eval(as, ch, {});
                u_ev_opt_info u_opt2 = right->compile_eval(as, ch, {});
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            case Operator::DISCARD: {
                ch.put_instruction(Opcode::SEP);
                u_ev_opt_info u_opt1 = left->compile_eval(as, ch, {});
                ch.put_instruction(Opcode::DIS);
                u_ev_opt_info u_opt2 = right->compile_eval(as, ch, {});
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            case Operator::LAMBDA: {
                auto &new_ch = as.new_chunk(); // Chunk of the new function
                ch.put_instruction({Opcode::VAL, static_cast<uint16_t>(Type::FUN), 0});
                as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), new_ch.id, 0);
                // Here goes the bytecode of new function
                new_ch.put_instruction({Opcode::SCP, true}); // Create the scope of the function
                u_mv_opt_info u_opt1 = left->compile_move(as, new_ch, {}); // Assign function arguments
                new_ch.put_instruction({Opcode::DIS, true}); // Discard the separator after the arguments
                u_ev_opt_info u_opt2 = right->compile_eval(as, new_ch, {}); // Evaluate function body
                new_ch.put_instruction({Opcode::SCP, false}); // Discard the scope ot the function
                new_ch.put_instruction(Opcode::END);
                return {.no_scope = true};
            }
            case Operator::INDEX: {
                ch.put_instruction(Opcode::SEP);
                u_ev_opt_info u_opt0 = left->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::GET, false, 0 /* Will be overwritten to actual name location */});
                as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), 0, as.add_string(right->get_identifier()));
                return {.no_scope = false};
            }
            case Operator::THEN: {
                ch.put_instruction(Opcode::SEP);
                u_ev_opt_info u_opt1 = left->compile_eval(as, ch, {});
                auto pos = ch.put_instruction(); // Position of jump instruction (over `then` expression)
                u_ev_opt_info u_opt2 = right->compile_eval(as, ch, {});
                ch.set_instruction(pos, Opcode::JNO);
                as.add_pointer(ch.id, pos + sizeof(Instruction) - sizeof(Instruction::u64), ch.id, ch.size());
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            case Operator::ELSE: {
                auto [cond, then] = left->get_then();
                ch.put_instruction(Opcode::SEP);
                u_ev_opt_info u_opt1 = cond->compile_eval(as, ch, {});
                auto pos1 = ch.put_instruction(); // Position of jump instruction (over `then` expression)
                u_ev_opt_info u_opt2 = then->compile_eval(as, ch, {});
                auto pos2 = ch.put_instruction(); // Position of jump instruction (over `else` expression)
                ch.set_instruction(pos1, Opcode::JNO);
                as.add_pointer(ch.id, pos1 + sizeof(Instruction) - sizeof(Instruction::u64), ch.id, ch.size());
                u_ev_opt_info u_opt3 = right->compile_eval(as, ch, {});
                ch.set_instruction(pos2, Opcode::JMP);
                as.add_pointer(ch.id, pos2 + sizeof(Instruction) - sizeof(Instruction::u64), ch.id, ch.size());
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope && u_opt3.no_scope};
            }
            case Operator::UNTIL: {
                auto pos = ch.size(); // Position of where to jump back
                u_ev_opt_info u_opt1 = left->compile_eval(as, ch, {});
                ch.put_instruction(Opcode::SEP);
                u_ev_opt_info u_opt2 = right->compile_eval(as, ch, {});
                ch.put_instruction(Opcode::JNO);
                as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), ch.id, pos);
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            case Operator::DO: {
                auto pos0 = ch.size(); // Position of where to jump back
                ch.put_instruction(Opcode::SEP);
                u_ev_opt_info u_opt1 = left->compile_eval(as, ch, {});
                auto pos1 = ch.put_instruction(); // Position of jump instruction (over the body of the cycle)
                u_ev_opt_info u_opt2 = right->compile_eval(as, ch, {});
                ch.put_instruction(Opcode::JMP);
                as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), ch.id, pos0);
                ch.set_instruction(pos1, Opcode::JNO);
                as.add_pointer(ch.id, pos1 + sizeof(Instruction) - sizeof(Instruction::u64), ch.id, ch.size());
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            case Operator::AND: {
                ch.put_instruction(Opcode::SEP);
                u_ev_opt_info u_opt1 = left->compile_eval(as, ch, {});
                ch.put_instruction(Opcode::DUP); // Preserve the value before implicitly converting it to boolean
                auto pos = ch.put_instruction(); // Position of jump instruction (over the right operand)
                ch.put_instruction(Opcode::DIS);
                ch.put_instruction(Opcode::SEP);
                u_ev_opt_info u_opt2 = right->compile_eval(as, ch, {});
                ch.set_instruction(pos, Opcode::JNO);
                as.add_pointer(ch.id, pos + sizeof(Instruction) - sizeof(Instruction::u64), ch.id, ch.size());
                ch.put_instruction(Opcode::REM);
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            case Operator::OR: {
                ch.put_instruction(Opcode::SEP);
                u_ev_opt_info u_opt1 = left->compile_eval(as, ch, {});
                ch.put_instruction(Opcode::DUP); // Preserve the value before implicitly converting it to boolean
                auto pos = ch.put_instruction(); // Position of jump instruction (over the right operand)
                ch.put_instruction(Opcode::DIS);
                ch.put_instruction(Opcode::SEP);
                u_ev_opt_info u_opt2 = right->compile_eval(as, ch, {});
                ch.set_instruction(pos, Opcode::JYS);
                as.add_pointer(ch.id, pos + sizeof(Instruction) - sizeof(Instruction::u64), ch.id, ch.size());
                ch.put_instruction(Opcode::REM);
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            default: {
                ch.put_instruction({Opcode::SEP});
                u_ev_opt_info u_opt1 = right->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::SEP});
                u_ev_opt_info u_opt2 = left->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::OPR, static_cast<uint16_t>(op)});
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
                ch.put_instruction(Opcode::SEP);
                u_ev_opt_info u_opt0 = left->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::SET, false, 0 /* Will be overwritten to actual name location */});
                as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), 0, as.add_string(right->get_identifier()));
                return {.no_scope = false};
            }
            case Operator::CALL: {
                ch.put_instruction(Opcode::SEP);
                u_ev_opt_info u_opt1 = right->compile_eval(as, ch, {});
                ch.put_instruction(Opcode::SEP);
                u_ev_opt_info u_opt2 = left->compile_eval(as, ch, {});
                ch.put_instruction(Opcode::MOV);
                return {.no_scope = u_opt1.no_scope && u_opt2.no_scope};
            }
            default:
                throw CompilationError("expression is not assignable");
        }
    }

    OperatorAST::OperatorAST(funscript::AST *left, funscript::AST *right, funscript::Operator op) : left(left),
                                                                                                    right(right),
                                                                                                    op(op) {}

    std::pair<AST *, AST *> OperatorAST::get_then() const {
        return {left.get(), right.get()};
    }

    u_ev_opt_info NulAST::compile_eval(Assembler &as, Assembler::Chunk &ch, const d_ev_opt_info &d_opt) {
        ch.put_instruction({Opcode::VAL, static_cast<uint16_t>(Type::NUL)});
        return {.no_scope = true};
    }

    u_mv_opt_info NulAST::compile_move(Assembler &as, Assembler::Chunk &ch, const d_mv_opt_info &d_opt) {
        throw CompilationError("expression is not assignable");
    }

    NulAST::NulAST() = default;

    u_ev_opt_info VoidAST::compile_eval(Assembler &as, Assembler::Chunk &ch, const d_ev_opt_info &d_opt) {
        return {.no_scope = true};
    }

    u_mv_opt_info VoidAST::compile_move(Assembler &as, Assembler::Chunk &ch, const d_mv_opt_info &d_opt) {
        return {.no_scope = true};
    }

    VoidAST::VoidAST() = default;

    u_ev_opt_info BracketAST::compile_eval(Assembler &as, Assembler::Chunk &ch, const d_ev_opt_info &d_opt) {
        u_ev_opt_info u_opt0;
        switch (type) {
            case Bracket::PLAIN: {
                size_t scp_pos = ch.put_instruction({Opcode::SCP, true});
                u_opt0 = child->compile_eval(as, ch, {});
                ch.put_instruction({u_opt0.no_scope ? Opcode::NOP : Opcode::SCP, false});
                if (u_opt0.no_scope) ch.set_instruction(scp_pos, Opcode::NOP);
                break;
            }
            case Bracket::CURLY: {
                ch.put_instruction({Opcode::SCP, true}); // Create object scope
                ch.put_instruction({Opcode::SEP});
                u_opt0 = child->compile_eval(as, ch, {});
                ch.put_instruction({Opcode::DIS}); // Discard all values produced by sub-expression
                ch.put_instruction({Opcode::VAL, static_cast<uint16_t>(Type::OBJ)}); // Create an object from scope
                ch.put_instruction({Opcode::SCP, false}); // Discard object scope
                break;
            }
            case Bracket::SQUARE: {
                size_t scp_pos = ch.put_instruction({Opcode::SCP, true});
                ch.put_instruction(Opcode::SEP);
                u_opt0 = child->compile_eval(as, ch, {});
                ch.put_instruction(Opcode::ARR);
                ch.put_instruction({u_opt0.no_scope ? Opcode::NOP : Opcode::SCP, false});
                if (u_opt0.no_scope) ch.set_instruction(scp_pos, Opcode::NOP);
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
                throw CompilationError("expression is not assignable");
        }
        return {.no_scope = u_opt0.no_scope};
    }

    BracketAST::BracketAST(funscript::AST *child, funscript::Bracket type) : type(type), child(child) {}

    u_ev_opt_info BooleanAST::compile_eval(Assembler &as, Assembler::Chunk &ch, const d_ev_opt_info &d_opt) {
        ch.put_instruction({Opcode::VAL, static_cast<uint16_t>(Type::BLN), static_cast<uint64_t>(bln)});
        return {.no_scope = true};
    }

    u_mv_opt_info BooleanAST::compile_move(Assembler &as, Assembler::Chunk &ch, const d_mv_opt_info &d_opt) {
        throw CompilationError("expression is not assignable");
    }

    BooleanAST::BooleanAST(bool bln) : bln(bln) {}

    StringAST::StringAST(std::string str) : str(std::move(str)) {}

    u_ev_opt_info StringAST::compile_eval(Assembler &as, Assembler::Chunk &ch, const d_ev_opt_info &d_opt) {
        ch.put_instruction({Opcode::STR,
                            static_cast<uint16_t>(str.size()), 0 /* Will be overwritten to actual string location */});
        as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), 0, as.add_string(str));
        return {.no_scope = true};
    }

    u_mv_opt_info StringAST::compile_move(Assembler &as, Assembler::Chunk &chunk, const d_mv_opt_info &d_opt) {
        throw CompilationError("expression is not assignable");
    }
}