//
// Created by avevad on 11/20/21.
//

#ifndef FUNSCRIPT_COMPILER_H
#define FUNSCRIPT_COMPILER_H

#include "common.h"

#include <utility>
#include <vector>
#include <memory>

namespace funscript {

    class Assembler;

    class AST {
        friend Assembler;
    public:
        virtual void compile_eval(Assembler &as, size_t cid) = 0;
        virtual void compile_move(Assembler &as, size_t cid) = 0;
        virtual ~AST() = default;
    };

    using ast_ptr = std::unique_ptr<AST>;

    ast_ptr parse(const std::vector<Token> &tokens);

    class Assembler {
        struct Relocation {
            size_t src_cid, src_pos; // where the relocation will point to
            size_t dst_cid, dst_pos; // where the relocation will be written to
        };

        std::vector<std::string> chunks;
        std::vector<Relocation> relocs;

    public:
        size_t new_chunk();

        void put_opcode(size_t cid, Opcode op);
        void put_int(size_t cid, int64_t num);
        void put_reloc(size_t cid, size_t pos, size_t dst_cid, size_t dst_pos);
        void put_reloc(size_t cid, size_t dst_cid, size_t dst_pos);
        void put_data(size_t cid, const char *data, size_t size);
        size_t put_string(size_t cid, const std::wstring &str);
        void put_byte(size_t cid, char c);

        [[nodiscard]] size_t total_size() const;
        [[nodiscard]] size_t chunk_size(size_t cid) const;

        Assembler();

        void clear() {
            chunks.clear();
            relocs.clear();
        }

        void compile_expression(AST *ast);
        void assemble(char *buffer);
    };

    class IntegerAST : public AST {
        int64_t num;

        void compile_eval(Assembler &as, size_t cid) override;

        void compile_move(Assembler &as, size_t cid) override;
    public:
        explicit IntegerAST(int64_t num) : num(num) {}

    };

    class IdentifierAST : public AST {
        std::wstring name;

        void compile_eval(Assembler &as, size_t cid) override;

        void compile_move(Assembler &as, size_t cid) override;
    public:
        explicit IdentifierAST(std::wstring name) : name(std::move(name)) {}

    };

    class OperatorAST : public AST {
        ast_ptr left{}, right{};
        Operator op;

        void compile_eval(Assembler &as, size_t cid) override;

        void compile_move(Assembler &as, size_t cid) override;
    public:
        OperatorAST(AST *left, AST *right, Operator op) : left(left), right(right), op(op) {}

    };

    class NulAST : public AST {
        void compile_eval(Assembler &as, size_t cid) override;

        void compile_move(Assembler &as, size_t cid) override;
    public:
        NulAST() = default;
    };

    class VoidAST : public AST {
        void compile_eval(Assembler &as, size_t cid) override;

        void compile_move(Assembler &as, size_t cid) override;
    };

    class BracketAST : public AST {
        Bracket type;
        ast_ptr child;
        void compile_eval(Assembler &as, size_t cid) override;

        void compile_move(Assembler &as, size_t cid) override;
    public:
        BracketAST(AST *child, Bracket type) : type(type), child(child) {}
    };

    class IndexAST : public AST {
        ast_ptr child;
        std::wstring name;

        void compile_eval(Assembler &as, size_t cid) override;

        void compile_move(Assembler &as, size_t cid) override;
    public:
        IndexAST(AST *child, std::wstring name) : child(child), name(std::move(name)) {}
    };
}

#endif //FUNSCRIPT_COMPILER_H
