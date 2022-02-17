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

    class AST;

    class Assembler {
    public:
        class Chunk {
            friend Assembler;
            std::string data;

            Chunk(size_t id) : id(id) {}

            template<typename T>
            void put(T x = T()) { data.append(reinterpret_cast<const char *>(&x), sizeof x); }

            template<typename T>
            size_t align() {
                if (data.length() % sizeof(T) != 0) data.append(sizeof(T) - data.length() % sizeof(T), '\0');
                return data.size();
            }

            template<typename T>
            size_t put_aligned(T x = T()) {
                auto pos = align<T>();
                put(x);
                return pos;
            }

        public:
            const size_t id;

            size_t put_instruction(Instruction ins = {.op = Opcode::NOP});
            void set_instruction(size_t pos, Instruction ins = {.op = Opcode::NOP});

            size_t size() const { return data.length(); }
        };

    private:
        static const constexpr size_t DATA = 0, CONST = 1;
        struct pointer {
            size_t from_chunk, from_pos;
            size_t to_chunk, to_pos;
        };
        std::vector<Chunk *> chunks;
        std::vector<pointer> pointers;

        void add_pointer(size_t from_chunk, size_t from_pos, size_t to_chunk, size_t to_pos);

        void delete_chunks();

    public:
        Chunk &new_chunk();

        uint16_t add_pointer_constant(Chunk &to_chunk, size_t to_pos);
        uint16_t add_int_constant(int64_t n);
        uint16_t add_string_constant(const std::wstring &str);

        void compile_expression(AST *ast);

        [[nodiscard]] size_t total_size() const;

        void assemble(char *buffer) {
            std::vector<size_t> chunks_pos(chunks.size());
            size_t pos = 0;
            for (size_t i = 0; i < chunks.size(); i++) {
                size_t ch_id = (i + 1) % chunks.size(); // shift to place DATA chunk at the end
                chunks_pos[ch_id] = pos;
                memcpy(buffer + pos, chunks[ch_id]->data.data(), chunks[ch_id]->size());
                pos += chunks[ch_id]->size();
            }
            for (auto[from_chunk, from_pos, to_chunk, to_pos]: pointers) {
                size_t to_real_pos = chunks_pos[to_chunk] + to_pos;
                size_t from_real_pos = chunks_pos[from_chunk] + from_pos;
                memcpy(buffer + from_real_pos, &to_real_pos, sizeof to_real_pos);
            }
        }

        ~Assembler();
    };

    struct eval_opt_info {
        bool no_scope = false;
    };

    class AST {
        friend Assembler;
    public:
        const eval_opt_info eval_opt;

        virtual void compile_eval(Assembler &as, Assembler::Chunk &chunk) = 0;
        virtual void compile_move(Assembler &as, Assembler::Chunk &chunk) = 0;

        virtual std::pair<AST *, AST *> get_then_operator() {
            assert_failed("not a 'then' operator"); // TODO
            return {};
        }

        explicit AST(const eval_opt_info &eval_opt) : eval_opt(eval_opt) {};

        virtual ~AST() = default;
    };

    using ast_ptr = std::unique_ptr<AST>;

    ast_ptr parse(const std::vector<Token> &tokens);

    class IntegerAST : public AST {
        int64_t num;

        void compile_eval(Assembler &as, Assembler::Chunk &chunk) override;

        void compile_move(Assembler &as, Assembler::Chunk &chunk) override;
    public:
        explicit IntegerAST(int64_t num) : AST({.no_scope = true}), num(num) {}

    };

    class IdentifierAST : public AST {
        std::wstring name;

        void compile_eval(Assembler &as, Assembler::Chunk &chunk) override;

        void compile_move(Assembler &as, Assembler::Chunk &chunk) override;
    public:
        explicit IdentifierAST(std::wstring name) : AST({.no_scope = true}), name(std::move(name)) {}

    };

    class OperatorAST : public AST {
        ast_ptr left{}, right{};
        Operator op;

        void compile_eval(Assembler &as, Assembler::Chunk &chunk) override;

        void compile_move(Assembler &as, Assembler::Chunk &chunk) override;

        std::pair<AST *, AST *> get_then_operator() override {
            if (op != Operator::THEN) return AST::get_then_operator();
            return {left.get(), right.get()};
        }

    public:
        OperatorAST(AST *left, AST *right, Operator op);

    };

    class NulAST : public AST {
        void compile_eval(Assembler &as, Assembler::Chunk &chunk) override;

        void compile_move(Assembler &as, Assembler::Chunk &chunk) override;

    public:
        NulAST() : AST({.no_scope = true}) {}
    };

    class VoidAST : public AST {
        void compile_eval(Assembler &as, Assembler::Chunk &chunk) override;

        void compile_move(Assembler &as, Assembler::Chunk &chunk) override;

    public:
        VoidAST() : AST({.no_scope = true}) {}
    };

    class BracketAST : public AST {
        Bracket type;
        ast_ptr child;
        void compile_eval(Assembler &as, Assembler::Chunk &chunk) override;

        void compile_move(Assembler &as, Assembler::Chunk &chunk) override;
    public:
        BracketAST(AST *child, Bracket type) : AST({.no_scope = true}), type(type), child(child) {}
    };

    class IndexAST : public AST {
        ast_ptr child;
        std::wstring name;

        void compile_eval(Assembler &as, Assembler::Chunk &chunk) override;

        void compile_move(Assembler &as, Assembler::Chunk &chunk) override;
    public:
        IndexAST(AST *child, std::wstring name) : AST({.no_scope = true}), child(child), name(std::move(name)) {}
    };

    class BooleanAST : public AST {
        bool bln;

        void compile_eval(Assembler &as, Assembler::Chunk &chunk) override;

        void compile_move(Assembler &as, Assembler::Chunk &chunk) override;
    public:
        explicit BooleanAST(bool bln) : AST({.no_scope = true}), bln(bln) {}

    };
}

#endif //FUNSCRIPT_COMPILER_H
