//
// Created by avevad on 11/20/21.
//

#include "compiler.h"
#include "common.h"

#include <stdexcept>
#include <cstring>

namespace funscript {

    bool insert_void_after(Token::Type type) {
        return type == Token::OPERATOR || type == Token::LEFT_BRACKET;
    }

    bool insert_call_after(Token::Type type) {
        return !insert_void_after(type);
    }


    ast_ptr parse(const std::vector<Token> &tokens) {
        if (tokens.empty()) return ast_ptr(new VoidAST);
        std::vector<Token> stack, queue;
        for (size_t pos = 0; pos < tokens.size(); pos++) {
            const auto &token = tokens[pos];
            switch (token.type) {
                case Token::NUL:
                case Token::INTEGER:
                case Token::BOOLEAN:
                case Token::STRING:
                case Token::ID: {
                    if (pos != 0 && insert_call_after(tokens[pos - 1].type)) {
                        while (!stack.empty() && stack.back().type == Token::INDEX) {
                            queue.push_back(stack.back());
                            stack.pop_back();
                        }
                        stack.push_back({Token::OPERATOR, Operator::CALL});
                    }
                    queue.push_back(token);
                    break;
                }
                case Token::OPERATOR: {
                    if (pos == 0 || insert_void_after(tokens[pos - 1].type)) queue.push_back({Token::VOID, 0});
                    OperatorMeta op1 = OPERATORS.at(std::get<Operator>(token.data));
                    while (!stack.empty()) {
                        Token top = stack.back();
                        if (top.type == Token::INDEX) {
                            stack.pop_back();
                            queue.push_back(top);
                            continue;
                        }
                        if (top.type != Token::OPERATOR) break;
                        OperatorMeta op2 = OPERATORS.at(std::get<Operator>(top.data));
                        if (op2.order < op1.order || (op2.order == op1.order && op1.left)) {
                            stack.pop_back();
                            queue.push_back(top);
                        } else break;
                    }
                    stack.push_back(token);
                    break;
                }
                case Token::LEFT_BRACKET: {
                    if (pos != 0 && insert_call_after(tokens[pos - 1].type)) {
                        while (!stack.empty() && stack.back().type == Token::INDEX) {
                            queue.push_back(stack.back());
                            stack.pop_back();
                        }
                        stack.push_back({Token::OPERATOR, Operator::CALL});
                    }
                    stack.push_back(token);
                    break;
                }
                case Token::RIGHT_BRACKET: {
                    Bracket br = get<Bracket>(token.data);
                    if (pos != 0 && insert_void_after(tokens[pos - 1].type)) queue.push_back({Token::VOID, 0});
                    while (!stack.empty() && stack.back().type != Token::LEFT_BRACKET) {
                        queue.push_back(stack.back());
                        stack.pop_back();
                    }
                    if (stack.empty()) throw CompilationError("unmatched right bracket");
                    if (get<Bracket>(stack.back().data) != br) throw CompilationError("brackets do not match");
                    stack.pop_back();
                    queue.push_back({Token::RIGHT_BRACKET, br});
                    break;
                }
                case Token::VOID:
                case Token::UNKNOWN:
                    throw CompilationError("unknown token");
                case Token::INDEX:
                    if (pos == 0 || insert_void_after(tokens[pos - 1].type)) queue.push_back({Token::VOID, 0});
                    while (!stack.empty()) {
                        Token top = stack.back();
                        if (top.type != Token::INDEX) break;
                        stack.pop_back();
                        queue.push_back(top);
                    }
                    stack.push_back(token);
                    break;
            }
        }
        if (insert_void_after(tokens.back().type)) queue.push_back({Token::VOID, 0});
        while (!stack.empty()) {
            if (stack.back().type == Token::LEFT_BRACKET) throw CompilationError("unmatched left bracket");
            queue.push_back(stack.back());
            stack.pop_back();
        }
        std::vector<AST *> ast;
        for (const Token &token: queue) {
            switch (token.type) {
                case Token::STRING: {
                    ast.push_back(new StringAST(get<std::wstring>(token.data)));
                    break;
                }
                case Token::NUL: {
                    ast.push_back(new NulAST);
                    break;
                }
                case Token::ID: {
                    ast.push_back(new IdentifierAST(get<std::wstring>(token.data)));
                    break;
                }
                case Token::INTEGER: {
                    ast.push_back(new IntegerAST(get<int64_t>(token.data)));
                    break;
                }
                case Token::OPERATOR: {
                    if (ast.empty()) throw CompilationError("missing operand");
                    AST *right = ast.back();
                    ast.pop_back();
                    if (ast.empty()) throw CompilationError("missing operand");
                    AST *left = ast.back();
                    ast.pop_back();
                    ast.push_back(new OperatorAST(left, right, get<Operator>(token.data)));
                    break;
                }
                case Token::LEFT_BRACKET:
                    assert_failed("left bracket in output queue");
                case Token::RIGHT_BRACKET: {
                    AST *child = ast.back();
                    ast.pop_back();
                    ast.push_back(new BracketAST(child, std::get<Bracket>(token.data)));
                    break;
                }
                case Token::VOID: {
                    ast.push_back(new VoidAST);
                    break;
                }
                case Token::UNKNOWN: {
                    throw std::runtime_error("unknown token");
                }
                case Token::INDEX: {
                    AST *child = ast.back();
                    ast.pop_back();
                    ast.push_back(new IndexAST(child, get<std::wstring>(token.data)));
                    break;
                }
                case Token::BOOLEAN: {
                    ast.push_back(new BooleanAST(get<bool>(token.data)));
                    break;
                }
            }
        }
        if (ast.size() != 1) throw CompilationError("missing operator");
        return ast_ptr(ast[0]);
    }

    void OperatorAST::compile_eval(Assembler &as, Assembler::Chunk &ch) {
        switch (op) {
            case Operator::ASSIGN:
                ch.put_instruction({.op = Opcode::SEP});
                right->compile_eval(as, ch);
                left->compile_move(as, ch);
                ch.put_instruction({.op = Opcode::DIS});
                break;
            case Operator::APPEND:
                left->compile_eval(as, ch);
                right->compile_eval(as, ch);
                break;
            case Operator::DISCARD:
                ch.put_instruction({.op = Opcode::SEP});
                left->compile_eval(as, ch);
                ch.put_instruction({.op = Opcode::DIS});
                right->compile_eval(as, ch);
                break;
            case Operator::LAMBDA: {
                auto &new_ch = as.new_chunk(); // create new function chunk
                auto ptr_const = as.add_pointer_constant(new_ch, 0);
                ch.put_instruction({.op = Opcode::FUN, .u16 = ptr_const});
                // bytecode of the lambda function:
                new_ch.put_instruction({.op = Opcode::NS}); // create lambda scope
                left->compile_move(as, new_ch); // move arguments to their destination
                new_ch.put_instruction({.op = Opcode::DIS}); // discard preceding separator
                right->compile_eval(as, new_ch); // evaluate the lambda body
                new_ch.put_instruction({.op = Opcode::DS});; // discard lambda scope
                new_ch.put_instruction({.op = Opcode::END}); // return from new function
                break;
            }
            case Operator::THEN: {
                ch.put_instruction({.op = Opcode::SEP});
                left->compile_eval(as, ch);
                auto pos1 = ch.put_instruction();
                ch.put_instruction({.op = Opcode::DIS});
                right->compile_eval(as, ch);
                auto pos2 = ch.put_instruction();
                ch.set_instruction(pos1, {.op = Opcode::JN, .u16 = as.add_pointer_constant(ch, ch.size())});
                ch.put_instruction({.op = Opcode::DIS});
                ch.set_instruction(pos2, {.op = Opcode::JMP, .u16 = as.add_pointer_constant(ch, ch.size())});
                break;
            }
            case Operator::ELSE: {
                auto[cond, then] = left->get_then_operator();
                ch.put_instruction({.op = Opcode::SEP});
                cond->compile_eval(as, ch);
                auto pos1 = ch.put_instruction();
                ch.put_instruction({.op = Opcode::DIS});
                then->compile_eval(as, ch);
                auto pos2 = ch.put_instruction();
                ch.set_instruction(pos1, {.op = Opcode::JN, .u16 = as.add_pointer_constant(ch, ch.size())});
                ch.put_instruction({.op = Opcode::DIS});
                right->compile_eval(as, ch);
                ch.set_instruction(pos2, {.op = Opcode::JMP, .u16 = as.add_pointer_constant(ch, ch.size())});
                break;
            }
            case Operator::UNTIL: {
                ch.put_instruction({.op = Opcode::SEP});
                auto pos = ch.size();
                ch.put_instruction({.op = Opcode::DIS});
                left->compile_eval(as, ch);
                ch.put_instruction({.op = Opcode::SEP});
                right->compile_eval(as, ch);
                ch.put_instruction({.op = Opcode::JN, .u16 = as.add_pointer_constant(ch, pos)});
                ch.put_instruction({.op = Opcode::DIS});
                break;
            }
            case Operator::DO: {
                auto beg_pos = ch.size();
                ch.put_instruction({.op = Opcode::SEP});
                left->compile_eval(as, ch);
                auto stub_pos = ch.put_instruction();
                ch.put_instruction({.op = Opcode::DIS});
                right->compile_eval(as, ch);
                ch.put_instruction({.op = Opcode::JMP, .u16 = as.add_pointer_constant(ch, beg_pos)});
                ch.set_instruction(stub_pos, {.op = Opcode::JN, .u16 = as.add_pointer_constant(ch, ch.size())});
                ch.put_instruction({.op = Opcode::DIS});
                break;
            }
            default:
                ch.put_instruction({.op = Opcode::SEP});
                right->compile_eval(as, ch);
                ch.put_instruction({.op = Opcode::SEP});
                left->compile_eval(as, ch);
                ch.put_instruction({.op = Opcode::OP, .u8 = uint8_t(op)});
        }
    }

    void OperatorAST::compile_move(Assembler &as, Assembler::Chunk &ch) {
        switch (op) {
            case Operator::APPEND:
                right->compile_move(as, ch);
                left->compile_move(as, ch);
                break;
            default:
                throw CompilationError("expression is not assignable");
        }
    }

    static eval_opt_info merge_opt_info(Operator op, const eval_opt_info &a, const eval_opt_info &b) {
        switch (op) {
            case Operator::LAMBDA:
                return {.no_scope = true};
            case Operator::ASSIGN:
                return {.no_scope = false};
            default:
                return {.no_scope = a.no_scope && b.no_scope};
        }
    }

    OperatorAST::OperatorAST(AST *left, AST *right, Operator op) :
            AST(merge_opt_info(op, left->eval_opt, right->eval_opt)), left(left), right(right), op(op) {

    }

    void IdentifierAST::compile_eval(Assembler &as, Assembler::Chunk &ch) {
        ch.put_instruction({.op = Opcode::VGT, .u16 = as.add_string_constant(name)});
    }

    void IdentifierAST::compile_move(Assembler &as, Assembler::Chunk &ch) {
        ch.put_instruction({.op = Opcode::VST, .u16 = as.add_string_constant(name)});
    }

    void IntegerAST::compile_eval(Assembler &as, Assembler::Chunk &ch) {
        ch.put_instruction({.op = Opcode::INT, .u16 = as.add_int_constant(num)});
    }

    void IntegerAST::compile_move(Assembler &as, Assembler::Chunk &ch) {
        throw CompilationError("expression is not assignable");
    }

    void NulAST::compile_eval(Assembler &as, Assembler::Chunk &ch) {
        ch.put_instruction({.op = Opcode::NUL});
    }

    void NulAST::compile_move(Assembler &as, Assembler::Chunk &ch) {
        throw CompilationError("expression is not assignable");
    }

    void VoidAST::compile_eval(Assembler &as, Assembler::Chunk &ch) {}

    void VoidAST::compile_move(Assembler &as, Assembler::Chunk &ch) {

    }

    void BracketAST::compile_eval(Assembler &as, Assembler::Chunk &ch) {
        switch (type) {
            case Bracket::PLAIN:
                if (!child->eval_opt.no_scope) ch.put_instruction({.op = Opcode::NS});
                child->compile_eval(as, ch);
                if (!child->eval_opt.no_scope) ch.put_instruction({.op = Opcode::DS});
                break;
            case Bracket::CURLY:
                ch.put_instruction({.op = Opcode::NS});
                ch.put_instruction({.op = Opcode::SEP}); // to discard values returned by child
                child->compile_eval(as, ch);
                ch.put_instruction({.op = Opcode::DIS}); // discarding child values
                ch.put_instruction({.op = Opcode::OBJ});
                ch.put_instruction({.op = Opcode::DS});
                break;
            case Bracket::SQUARE:
                ch.put_instruction({.op = Opcode::SEP});
                child->compile_eval(as, ch);
                ch.put_instruction({.op = Opcode::ARR});
                break;
        }
    }

    void BracketAST::compile_move(Assembler &as, Assembler::Chunk &ch) {
        switch (type) {
            case Bracket::PLAIN:
                child->compile_move(as, ch);
                break;
            case Bracket::CURLY:
            case Bracket::SQUARE:
                throw CompilationError("expression is not assignable");
        }
    }

    void IndexAST::compile_eval(Assembler &as, Assembler::Chunk &ch) {
        ch.put_instruction({.op = Opcode::SEP});
        child->compile_eval(as, ch);
        ch.put_instruction({.op = Opcode::GET, .u16 = as.add_string_constant(name)});
    }

    void IndexAST::compile_move(Assembler &as, Assembler::Chunk &ch) {
        ch.put_instruction({.op = Opcode::SEP});
        child->compile_eval(as, ch);
        ch.put_instruction({.op = Opcode::SET, .u16 = as.add_string_constant(name)});
    }

    void BooleanAST::compile_eval(Assembler &as, Assembler::Chunk &ch) {
        ch.put_instruction({.op = bln ? Opcode::PBY : Opcode::PBN});
    }

    void BooleanAST::compile_move(Assembler &as, Assembler::Chunk &ch) {
        throw CompilationError("expression is not assignable");
    }

    size_t Assembler::Chunk::put_instruction(Instruction ins) {
        auto pos = size();
        data.append(reinterpret_cast<char *>(&ins), sizeof(ins));
        return pos;
    }

    void Assembler::Chunk::set_instruction(size_t pos, Instruction ins) {
        memcpy(data.data() + pos, &ins, sizeof ins);
    }


    void Assembler::add_pointer(size_t from_chunk, size_t from_pos, size_t to_chunk, size_t to_pos) {
        pointers.push_back({from_chunk, from_pos, to_chunk, to_pos});
    }

    uint16_t Assembler::add_pointer_constant(Assembler::Chunk &to_chunk, size_t to_pos) {
        size_t const_id;
        add_pointer(CONST, const_id = chunks[CONST]->put_aligned<size_t>(), to_chunk.id, to_pos);
        const_id /= sizeof(size_t);
        return const_id;
    }

    Assembler::Chunk &Assembler::new_chunk() {
        auto *new_ch = new Chunk(chunks.size());
        chunks.push_back(new_ch);
        return *new_ch;
    }

    Assembler::~Assembler() {
        delete_chunks();
    }

    uint16_t Assembler::add_int_constant(int64_t n) {
        return chunks[CONST]->put_aligned(n) / sizeof(int64_t);
    }

    uint16_t Assembler::add_string_constant(const std::wstring &str) {
        auto pos = chunks[DATA]->align<wchar_t>();
        chunks[DATA]->data.append(reinterpret_cast<const char *>(str.data()), str.size() * sizeof(wchar_t));
        chunks[DATA]->data.append(sizeof(wchar_t), 0);
        size_t const_id;
        add_pointer(CONST, const_id = chunks[CONST]->put_aligned<size_t>(), DATA, pos);
        const_id /= sizeof(size_t);
        return const_id;
    }

    void Assembler::delete_chunks() {
        for (auto *ch: chunks) delete ch;
    }

    void Assembler::compile_expression(AST *ast) {
        delete_chunks();
        chunks.clear();
        pointers.clear();
        new_chunk(); // DATA
        new_chunk(); // CONST
        chunks[CONST]->put<size_t>(); // stub for CONST size
        auto &ch = new_chunk(); // main chunk
        ast->compile_eval(*this, ch);
        ch.put_instruction({.op = Opcode::END});
        *reinterpret_cast<size_t *>(chunks[CONST]->data.data()) = chunks[CONST]->size();
    }

    size_t Assembler::total_size() const {
        size_t size = 0;
        for (auto *ch: chunks) size += ch->size();
        return size;
    }

    void StringAST::compile_eval(Assembler &as, Assembler::Chunk &ch) {
        ch.put_instruction({.op = Opcode::STR, .u16 = as.add_string_constant(str)});
    }

    void StringAST::compile_move(Assembler &as, Assembler::Chunk &ch) {
        throw CompilationError("expression is not assignable");
    }
}
