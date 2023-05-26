#ifndef FUNSCRIPT_AST_H
#define FUNSCRIPT_AST_H

#include "tokenizer.h"
#include "common.h"
#include <memory>

namespace funscript {

    class AST;

    /**
     * Class that manages bytecode manipulation during AST compilation.
     * It holds a draft of produced bytecode and is able to assemble its parts into one executable array of code.
     */
    class Assembler {
    public:
        /**
         * Class that holds bytes of a code chunk.
         * Every function is a distinct chunk of code, which can be referenced from other chunks.
         * There is also a bytes chunk which contains every string used in the code.
         */
        class Chunk {
            friend Assembler;

            std::string bytes; // Contents of the chunk.

            Chunk(size_t id);

            /**
             * Appends raw presentation of any bytes at the end of the chunk content.
             * @tparam T Type of the bytes.
             * @param x The bytes to be appended.
             */
            template<typename T>
            void put(T x = T()) { bytes.append(reinterpret_cast<const char *>(&x), sizeof x); }

        public:
            const size_t id;

            /**
             * Appends raw presentation of instruction at the end of chunk content.
             * @param ins The instruction to be appended.
             * @return Final size of the chunk.
             */
            size_t put_instruction(Instruction ins = Opcode::NOP);

            /**
             * Puts raw presentation of instruction at the specified offset of chunk bytes.
             * @param pos The offset.
             * @param ins The instruction to be put.
             */
            void set_instruction(size_t pos, Instruction ins = Opcode::NOP);

            /**
             * @return Current size of chunk content.
             */
            [[nodiscard]] size_t size() const;
        };

    private:
        static const constexpr size_t DATA = 0; // The ID of the bytes chunk which is always zero

        /**
         * @brief The structure that holds info about deferred insertion of pointer.
         *
         * Sometimes it is needed to put some pointer to the specific offset of final bytecode, which cannot be calculated in the middle of assembly.
         * In such cases, the assembler will place a stub in this place of the chunk and schedule the insertion of pointer at the end of the assembly.
         */
        struct pointer {
            size_t from_chunk, from_pos; // Place to put the pointer.
            size_t to_chunk, to_pos; // Place that the pointer should point to.
        };

        std::vector<std::unique_ptr<Chunk>> chunks; // Collection of code chunks.
        std::vector<pointer> pointers; // Collection of scheduled pointer insertions.
    public:
        /**
         * Schedules delayed pointer insertion.
         */
        void add_pointer(size_t from_chunk, size_t from_pos, size_t to_chunk, size_t to_pos);

        /**
         * Creates a new chunk.
         * @return Reference to the newly created chunk.
         */
        Chunk &new_chunk();

        /**
         * Appends a string to the end of bytes chunk and returns the position of it.
         * @param str A string to be appended.
         * @return The resulting offset of this string in the bytes chunk.
         */
        size_t add_string(const std::string &str);

        /**
         * Compiles parsed expression (populates internal collections of chunks and others).
         * @param ast The AST of the expression to compile.
         */
        void compile_expression(AST *ast);

        /**
         * Calculates the total size of all chunks after compilation.
         * @return Total size of bytecode.
         */
        [[nodiscard]] size_t total_size() const;

        /**
         * Finishes the process of compilation and produces the final bytecode of previously compiled expression.
         * @param buffer The buffer to store the final bytecode.
         */
        void assemble(char *buffer) const;
    };

    class CompilationError : public std::runtime_error {
    public:
        explicit CompilationError(const std::string &msg);
    };

    /**
     * Abstract class of an AST node, which represents some sub-expression of code.
     */
    class AST {
        friend Assembler;
    public:
        virtual void compile_eval(Assembler &as, Assembler::Chunk &chunk) = 0;
        virtual void compile_move(Assembler &as, Assembler::Chunk &chunk) = 0;

        [[nodiscard]] virtual std::string get_identifier() const;
        [[nodiscard]] virtual std::pair<AST *, AST *> get_then() const;

        explicit AST();

        virtual ~AST() = default;
    };

    /**
     * Unique AST object pointer, because every pointer to an AST object is held only by its parent node.
     */
    using ast_ptr = std::unique_ptr<AST>;

    /**
     * Parses a stream of code tokens into an AST node of the whole expression.
     * @param tokens Vector of code tokens.
     * @return Resulting AST node.
     */
    ast_ptr parse(const std::vector<Token> &tokens);

    /**
     * A structure that holds some static operator metadata
     */
    struct OperatorMeta {
        int order; // Precedence of an operator (less value - higher precedence)
        bool left; // Whether it is a left-associative operator
    };

    /**
     * @return The mapping from operators to their metadata
     */
    static const std::unordered_map<Operator, OperatorMeta> &get_operators_meta() {
        static const std::unordered_map<Operator, OperatorMeta> OPERATORS{
                {Operator::INDEX,         {0,  true}},
                {Operator::CALL,          {0,  0 /* should not be used */ }},
                {Operator::NOT,           {1,  false}},
                {Operator::TIMES,         {3,  true}},
                {Operator::DIVIDE,        {4,  true}},
                {Operator::MODULO,        {4,  true}},
                {Operator::PLUS,          {4,  true}},
                {Operator::MINUS,         {4,  true}},
                {Operator::EQUALS,        {6,  true}},
                {Operator::DIFFERS,       {6,  true}},
                {Operator::LESS,          {6,  true}},
                {Operator::GREATER,       {6,  true}},
                {Operator::LESS_EQUAL,    {6,  true}},
                {Operator::GREATER_EQUAL, {6,  true}},
                {Operator::LAMBDA,        {7,  false}},
                {Operator::APPEND,        {9,  false}},
                {Operator::ASSIGN,        {10, true}},
                {Operator::THEN,          {11, false}},
                {Operator::ELSE,          {12, false}},
                {Operator::UNTIL,         {13, false}},
                {Operator::DO,            {13, false}},
                {Operator::DISCARD,       {14, false}},
        };
        return OPERATORS;
    }

    /**
     * Class of AST leaves which represent integer literals.
     */
    class IntegerAST : public AST {
        int64_t num; // Number represented by the literal.

        void compile_eval(Assembler &as, Assembler::Chunk &chunk) override;
        void compile_move(Assembler &as, Assembler::Chunk &chunk) override;
    public:
        explicit IntegerAST(int64_t num);
    };

    /**
     * Class of AST leaves which represent identifiers.
     */
    class IdentifierAST : public AST {
        std::string name; // Name of the identifier.

        void compile_eval(Assembler &as, Assembler::Chunk &chunk) override;
        void compile_move(Assembler &as, Assembler::Chunk &chunk) override;

        std::string get_identifier() const override;
    public:
        explicit IdentifierAST(std::string name);
    };

    /**
     * Class of AST nodes which represent operator expressions.
     * These AST nodes have operator's operands as their children.
     */
    class OperatorAST : public AST {
        Operator op; // The operator of this expression.
        ast_ptr left{}, right{}; // Operands of the operator.

        void compile_eval(Assembler &as, Assembler::Chunk &chunk) override;
        void compile_move(Assembler &as, Assembler::Chunk &chunk) override;

        std::pair<AST *, AST *> get_then() const override;
    public:
        OperatorAST(AST *left, AST *right, Operator op);
    };

    /**
     * Class of AST leaves which represent `nul` literals.
     */
    class NulAST : public AST {
        void compile_eval(Assembler &as, Assembler::Chunk &chunk) override;
        void compile_move(Assembler &as, Assembler::Chunk &chunk) override;
    public:
        NulAST();
    };

    /**
     * Class of AST leaves which represent void literal.
     */
    class VoidAST : public AST {
        void compile_eval(Assembler &as, Assembler::Chunk &chunk) override;
        void compile_move(Assembler &as, Assembler::Chunk &chunk) override;
    public:
        VoidAST();
    };

    /**
     * Class of AST nodes which represent bracket expressions.
     * These AST nodes have brackets' sub-expression as their child.
     */
    class BracketAST : public AST {
        Bracket type; // Type of the brackets in this expression.
        ast_ptr child; // The sub-expression which is enclosed by brackets.
        void compile_eval(Assembler &as, Assembler::Chunk &chunk) override;
        void compile_move(Assembler &as, Assembler::Chunk &chunk) override;
    public:
        BracketAST(AST *child, Bracket type);
    };

    /**
     * Class of AST leaves which represent boolean literals.
     */
    class BooleanAST : public AST {
        bool bln; // Whether the literal represents `yes` value.

        void compile_eval(Assembler &as, Assembler::Chunk &chunk) override;
        void compile_move(Assembler &as, Assembler::Chunk &chunk) override;
    public:
        explicit BooleanAST(bool bln);
    };

    /**
     * Class of AST leaves which represent string literals.
     */
    class StringAST : public AST {
        std::string str;

        void compile_eval(Assembler &as, Assembler::Chunk &chunk) override;
        void compile_move(Assembler &as, Assembler::Chunk &chunk) override;
    public:
        explicit StringAST(std::string str);
    };
}

#endif //FUNSCRIPT_AST_H
