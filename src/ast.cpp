#include "ast.h"

namespace funscript {

    std::string AST::get_identifier() const {
        throw CompilationError("not an identifier");
    }

    std::pair<AST *, AST *> AST::get_then() const {
        throw CompilationError("not a `then` operator");
    }

    AST::AST(const funscript::eval_opt_info &eval_opt) : eval_opt(eval_opt) {}

    void IntegerAST::compile_eval(Assembler &as, Assembler::Chunk &ch) {
        ch.put_instruction({Opcode::VAL, static_cast<uint16_t>(Type::INT), static_cast<uint64_t>(num)});
    }

    void IntegerAST::compile_move(Assembler &as, Assembler::Chunk &ch) {
        throw CompilationError("expression is not assignable");
    }

    IntegerAST::IntegerAST(int64_t num) : AST({.no_scope = true}), num(num) {}

    void IdentifierAST::compile_eval(Assembler &as, Assembler::Chunk &ch) {
        ch.put_instruction({Opcode::SEP});
        ch.put_instruction({Opcode::GET, false, 0 /* Will be overwritten to actual name location */});
        as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), 0, as.add_string(name));
    }

    void IdentifierAST::compile_move(Assembler &as, Assembler::Chunk &ch) {
        ch.put_instruction({Opcode::SEP});
        ch.put_instruction({Opcode::SET, false, 0 /* Will be overwritten to actual name location */});
        as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), 0, as.add_string(name));
    }

    IdentifierAST::IdentifierAST(std::string name) : AST({.no_scope = true}), name(std::move(name)) {}

    std::string IdentifierAST::get_identifier() const {
        return name;
    }

    /**
     * Calculates optimization info of operator expression by combining its operands' optimization info.
     * @param op Operator of the expression.
     * @param a Optimization info of the left operand.
     * @param b Optimization info of the right operand.
     * @return Resulting optimization info.
     */
    static eval_opt_info merge_opt_info(Operator op, const eval_opt_info &a, const eval_opt_info &b) {
        switch (op) {
            case Operator::LAMBDA:
                return {.no_scope = true}; // We don't need redundant scopes for any lambda expressions
            case Operator::ASSIGN:
                return {.no_scope = false}; // Assignment operator can create a variable in current sub-scope, so we should create it
            default:
                // We should create a scope if either of operands require it
                return {.no_scope = a.no_scope && b.no_scope};
        }
    }

    void OperatorAST::compile_eval(Assembler &as, Assembler::Chunk &ch) {
        switch (op) {
            case Operator::ASSIGN: {
                ch.put_instruction(Opcode::SEP);
                right->compile_eval(as, ch);
                left->compile_move(as, ch);
                ch.put_instruction(Opcode::DIS);
                break;
            }
            case Operator::APPEND: {
                left->compile_eval(as, ch);
                right->compile_eval(as, ch);
                break;
            }
            case Operator::DISCARD: {
                ch.put_instruction(Opcode::SEP);
                left->compile_eval(as, ch);
                ch.put_instruction(Opcode::DIS);
                right->compile_eval(as, ch);
                break;
            }
            case Operator::LAMBDA: {
                auto &new_ch = as.new_chunk(); // Chunk of the new function
                ch.put_instruction({Opcode::VAL, static_cast<uint16_t>(Type::FUN), 0});
                as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), new_ch.id, 0);
                // Here goes the bytecode of new function
                new_ch.put_instruction({Opcode::SCP, true}); // Create the scope of the function
                left->compile_move(as, new_ch); // Assign function arguments
                new_ch.put_instruction(Opcode::DIS); // Discard the separator after the arguments
                right->compile_eval(as, new_ch); // Evaluate function body
                new_ch.put_instruction({Opcode::SCP, false}); // Discard the scope ot the function
                new_ch.put_instruction(Opcode::END);
                break;
            }
            case Operator::INDEX: {
                ch.put_instruction(Opcode::SEP);
                left->compile_eval(as, ch);
                ch.put_instruction({Opcode::GET, false, as.add_string(right->get_identifier())});
                break;
            }
            case Operator::THEN: {
                ch.put_instruction(Opcode::SEP);
                left->compile_eval(as, ch);
                auto pos = ch.put_instruction(); // Position of jump instruction (over `then` expression)
                right->compile_eval(as, ch);
                ch.set_instruction(pos, Opcode::JNO);
                as.add_pointer(ch.id, pos + sizeof(Instruction) - sizeof(Instruction::u64), ch.id, ch.size());
                break;
            }
            case Operator::ELSE: {
                auto [cond, then] = left->get_then();
                ch.put_instruction(Opcode::SEP);
                cond->compile_eval(as, ch);
                auto pos1 = ch.put_instruction(); // Position of jump instruction (over `then` expression)
                then->compile_eval(as, ch);
                auto pos2 = ch.put_instruction(); // Position of jump instruction (over `else` expression)
                ch.set_instruction(pos1, Opcode::JNO);
                as.add_pointer(ch.id, pos1 + sizeof(Instruction) - sizeof(Instruction::u64), ch.id, ch.size());
                right->compile_eval(as, ch);
                ch.set_instruction(pos2, Opcode::JMP);
                as.add_pointer(ch.id, pos2 + sizeof(Instruction) - sizeof(Instruction::u64), ch.id, ch.size());
                break;
            }
            case Operator::UNTIL: {
                auto pos = ch.size(); // Position of where to jump back
                left->compile_eval(as, ch);
                ch.put_instruction(Opcode::SEP);
                right->compile_eval(as, ch);
                ch.put_instruction(Opcode::JNO);
                as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), ch.id, pos);
                break;
            }
            case Operator::DO: {
                auto pos0 = ch.size(); // Position of where to jump back
                ch.put_instruction(Opcode::SEP);
                left->compile_eval(as, ch);
                auto pos1 = ch.put_instruction(); // Position of jump instruction (over the body of the cycle)
                right->compile_eval(as, ch);
                ch.put_instruction(Opcode::JMP);
                as.add_pointer(ch.id, ch.size() - sizeof(Instruction::u64), ch.id, pos0);
                ch.set_instruction(pos1, Opcode::JNO);
                as.add_pointer(ch.id, pos1 + sizeof(Instruction) - sizeof(Instruction::u64), ch.id, ch.size());
                break;
            }
            default: {
                ch.put_instruction({Opcode::SEP});
                right->compile_eval(as, ch);
                ch.put_instruction({Opcode::SEP});
                left->compile_eval(as, ch);
                ch.put_instruction({Opcode::OPR, static_cast<uint16_t>(op)});
                break;
            }
        }
    }

    void OperatorAST::compile_move(Assembler &as, Assembler::Chunk &ch) {
        switch (op) {
            case Operator::APPEND:
                right->compile_move(as, ch);
                left->compile_move(as, ch);
                break;
            case Operator::INDEX:
                ch.put_instruction(Opcode::SEP);
                left->compile_eval(as, ch);
                ch.put_instruction({Opcode::SET, false, as.add_string(right->get_identifier())});
                break;
            default:
                throw CompilationError("expression is not assignable");
        }
    }

    OperatorAST::OperatorAST(funscript::AST *left, funscript::AST *right, funscript::Operator op) :
            AST(merge_opt_info(op, left->eval_opt, right->eval_opt)), left(left), right(right), op(op) {}

    std::pair<AST *, AST *> OperatorAST::get_then() const {
        return {left.get(), right.get()};
    }

    void NulAST::compile_eval(Assembler &as, Assembler::Chunk &ch) {
        ch.put_instruction({Opcode::VAL, static_cast<uint16_t>(Type::NUL)});
    }

    void NulAST::compile_move(Assembler &as, Assembler::Chunk &ch) {
        throw CompilationError("expression is not assignable");
    }

    NulAST::NulAST() : AST({.no_scope = true}) {}

    void VoidAST::compile_eval(Assembler &as, Assembler::Chunk &ch) {}

    void VoidAST::compile_move(Assembler &as, Assembler::Chunk &ch) {
        throw CompilationError("expression is not assignable");
    }

    VoidAST::VoidAST() : AST({.no_scope = true}) {}

    void BracketAST::compile_eval(Assembler &as, Assembler::Chunk &ch) {
        switch (type) {
            case Bracket::PLAIN:
                if (!child->eval_opt.no_scope) ch.put_instruction({Opcode::SCP, true});
                child->compile_eval(as, ch);
                if (!child->eval_opt.no_scope) ch.put_instruction({Opcode::SCP, false});
                break;
            case Bracket::CURLY:
                ch.put_instruction({Opcode::SCP, true});
                ch.put_instruction({Opcode::SEP});
                child->compile_eval(as, ch);
                ch.put_instruction({Opcode::DIS});
                ch.put_instruction({Opcode::SCP, false});
                ch.put_instruction({Opcode::VAL, static_cast<uint16_t>(Type::OBJ)});
                break;
        }
    }

    void BracketAST::compile_move(Assembler &as, Assembler::Chunk &ch) {
        switch (type) {
            case Bracket::PLAIN:
                child->compile_move(as, ch);
                break;
            case Bracket::CURLY:
                throw CompilationError("expression is not assignable");
        }
    }

    BracketAST::BracketAST(funscript::AST *child, funscript::Bracket type) : AST({.no_scope = true}),
                                                                             type(type), child(child) {}

    void BooleanAST::compile_eval(Assembler &as, Assembler::Chunk &ch) {
        ch.put_instruction({Opcode::VAL, static_cast<uint16_t>(Type::BLN), static_cast<uint64_t>(bln)});
    }

    void BooleanAST::compile_move(Assembler &as, Assembler::Chunk &ch) {
        throw CompilationError("expression is not assignable");
    }

    BooleanAST::BooleanAST(bool bln) : AST({.no_scope = true}), bln(bln) {}
}