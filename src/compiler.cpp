//
// Created by avevad on 11/20/21.
//

#include "compiler.h"
#include "common.h"

#include <stdexcept>
#include <cstring>

namespace funscript {
    AST *parse(const std::vector<Token> &tokens) {
        std::vector<Token> stack, queue;
        for (const Token &token: tokens) {
            switch (token.type) {
                case Token::INTEGER:
                case Token::ID: {
                    queue.push_back(token);
                    break;
                }
                case Token::OPERATOR: {
                    OperatorMeta op1 = OPERATORS.at(std::get<Operator>(token.data));
                    while (!stack.empty()) {
                        Token top = stack.back();
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
                case Token::BRACKET: {
                    Bracket br = get<Bracket>(token.data);
                    if (br == Bracket::PLAIN) stack.push_back(token);
                    if (br == Bracket::PLAIN_R) {
                        while (!stack.empty() && stack.back().type != Token::BRACKET) {
                            queue.push_back(stack.back());
                            stack.pop_back();
                        }
                        if (stack.empty()) throw CompilationError("unmatched right bracket");
                        stack.pop_back();
                    }
                    break;
                }
                case Token::UNKNOWN: throw CompilationError("unknown token");
            }
        }
        while (!stack.empty()) {
            if (stack.back().type == Token::BRACKET) throw CompilationError("unmatched left bracket");
            queue.push_back(stack.back());
            stack.pop_back();
        }
        std::vector<AST *> ast;
        for (const Token &token: queue) {
            switch (token.type) {
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
                default: {
                    throw std::runtime_error("unknown token");
                }
            }
        }
        if (ast.size() != 1) throw CompilationError("missing operator");
        return ast[0];
    }

    size_t Assembler::new_chunk() {
        chunks.emplace_back();
        return chunks.size() - 1;
    }

    void Assembler::put_opcode(size_t cid, Opcode op) {
        chunks[cid] += char(op);
    }

    void Assembler::put_int(size_t cid, int64_t num) {
        chunks[cid].append(reinterpret_cast<char *>(&num), sizeof(int64_t));
    }

    void Assembler::put_reloc(size_t cid, size_t dst_cid, size_t dst_pos) {
        relocs.push_back({cid, chunks[cid].size(), dst_cid, dst_pos});
        chunks[cid].append(sizeof(size_t), '\0');
    }

    void Assembler::put_reloc(size_t cid, size_t pos, size_t dst_cid, size_t dst_pos) {
        relocs.push_back({cid, pos, dst_cid, dst_pos});
    }

    size_t Assembler::total_size() const {
        size_t pos = 0;
        size_t align = sizeof(max_align_t);
        for (size_t i = 1; i <= chunks.size(); i++) {
            size_t cid = i % chunks.size();
            pos += chunks[cid].size();
            if (pos % align != 0) pos += align - (pos % align);
        }
        return pos;
    }

    void Assembler::assemble(char *buffer) {
        std::vector<size_t> chunks_pos(chunks.size());
        size_t pos = 0;
        size_t align = sizeof(max_align_t);
        for (size_t i = 1; i <= chunks.size(); i++) {
            size_t cid = i % chunks.size();
            chunks_pos[cid] = pos;
            memcpy(buffer + pos, chunks[cid].data(), chunks[cid].size());
            pos += chunks[cid].size();
            if (pos % align != 0) pos += align - (pos % align);
        }
        for (Relocation reloc: relocs) {
            size_t result_pos = chunks_pos[reloc.dst_cid] + reloc.dst_pos;
            memcpy(buffer + chunks_pos[reloc.src_cid] + reloc.src_pos, &result_pos, sizeof(size_t));
        }
    }

    void Assembler::put_data(size_t cid, const char *data, size_t size) {
        chunks[cid].append(data, size);
    }

    size_t Assembler::chunk_size(size_t cid) const {
        return chunks[cid].size();
    }

    size_t Assembler::put_string(size_t cid, const std::wstring &str) {
        size_t align = sizeof(wchar_t);
        size_t pos = chunks[cid].length();
        if (pos % align != 0) chunks[cid].append(align - (pos % align), '\0');
        pos = chunks[cid].length();
        put_data(cid, reinterpret_cast<const char *>(str.c_str()), (str.size() + 1) * sizeof(wchar_t));
        return pos;
    }

    void Assembler::put_byte(size_t cid, char c) {
        chunks[cid].push_back(c);
    }

    Assembler::Assembler() {
        new_chunk(); // data chunk
    }

    void OperatorAST::compile_val(Assembler &as, size_t cid) {
        if (op == Operator::ASSIGN) {
            as.put_opcode(cid, Opcode::SEP);
            right->compile_val(as, cid);
            as.put_opcode(cid, Opcode::SEP);
            left->compile_ref(as, cid);
            as.put_opcode(cid, Opcode::MOV);
        } else if (op == Operator::APPEND) {
            left->compile_val(as, cid);
            right->compile_val(as, cid);
        } else if (op == Operator::DISCARD) {
            as.put_opcode(cid, Opcode::SEP);
            left->compile_val(as, cid);
            as.put_opcode(cid, Opcode::DIS);
            right->compile_val(as, cid);
        } else {
            as.put_opcode(cid, Opcode::SEP);
            left->compile_val(as, cid);
            as.put_opcode(cid, Opcode::SEP);
            right->compile_val(as, cid);
            as.put_opcode(cid, Opcode::OP);
            as.put_byte(cid, (char) op);
        }
    }

    void OperatorAST::compile_ref(Assembler &as, size_t cid) {
        if (op == Operator::APPEND) {
            left->compile_ref(as, cid);
            right->compile_ref(as, cid);
        } else if (op == Operator::DISCARD) {
            as.put_opcode(cid, Opcode::SEP);
            left->compile_val(as, cid);
            as.put_opcode(cid, Opcode::DIS);
            right->compile_ref(as, cid);
        }
    }

    void IdentifierAST::compile_val(Assembler &as, size_t cid) {
        as.put_opcode(cid, Opcode::VAL);
        as.put_reloc(cid, 0, as.put_string(0, name));
    }

    void IdentifierAST::compile_ref(Assembler &as, size_t cid) {
        as.put_opcode(cid, Opcode::REF);
        as.put_reloc(cid, 0, as.put_string(0, name));
    }

    void IntegerAST::compile_val(Assembler &as, size_t cid) {
        as.put_opcode(cid, Opcode::INT);
        as.put_int(cid, num);
    }

    void IntegerAST::compile_ref(Assembler &as, size_t cid) {
        throw std::runtime_error("expression is not assignable");
    }
}
