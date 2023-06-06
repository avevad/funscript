#ifndef FUNSCRIPT_AST_HPP
#define FUNSCRIPT_AST_HPP

#include "tokenizer.hpp"
#include "common.hpp"
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
        explicit CompilationError(const std::string &filename, const code_loc_t &loc, const std::string &msg);
    };

    /**
     * Structure that holds optimization info passed up to the higher level expression when generating evaluation bytecode.
     */
    struct u_ev_opt_info {
        bool no_scope = false;
    };

    /**
     * Structure that holds optimization info passed up to the higher level expression when generating assignment bytecode.
     */
    struct u_mv_opt_info {
        bool no_scope = false;
    };

    /**
     * Structure that holds optimization info passed down to the subexpressions when generating evaluation bytecode.
     */
    struct d_ev_opt_info {
    };

    /**
     * Structure that holds optimization info passed down to the subexpressions when generating assignment bytecode.
     */
    struct d_mv_opt_info {
    };

    /**
     * Abstract class of an AST node, which represents some sub-expression of code.
     */
    class AST {
        friend Assembler;
    public:
        const std::string filename; // Name of the source file of the expression.
        const code_loc_t token_loc; // Location of the token which forms the expression.

        virtual u_ev_opt_info compile_eval(Assembler &as, Assembler::Chunk &chunk, const d_ev_opt_info &d_opt) = 0;
        virtual u_mv_opt_info compile_move(Assembler &as, Assembler::Chunk &chunk, const d_mv_opt_info &d_opt) = 0;

        [[nodiscard]] virtual std::string get_identifier() const;
        [[nodiscard]] virtual std::pair<AST *, AST *> get_then() const;

        /**
         * @return Full source location of the whole expression.
         */
        [[nodiscard]] virtual code_loc_t get_location() const;

        explicit AST(std::string filename, code_loc_t token_loc);

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
    ast_ptr parse(const std::string &filename, std::vector<Token> tokens);

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
                {Operator::AND,           {7,  true}},
                {Operator::OR,            {8,  true}},
                {Operator::LAMBDA,        {9,  false}},
                {Operator::APPEND,        {10, false}},
                {Operator::ASSIGN,        {11, true}},
                {Operator::THEN,          {12, false}},
                {Operator::ELSE,          {13, false}},
                {Operator::UNTIL,         {14, false}},
                {Operator::DO,            {14, false}},
                {Operator::DISCARD,       {15, false}},
        };
        return OPERATORS;
    }

    /**
     * Class of AST leaves which represent integer literals.
     */
    class IntegerAST : public AST {
        int64_t num; // Number represented by the literal.

        u_ev_opt_info compile_eval(Assembler &as, Assembler::Chunk &chunk, const d_ev_opt_info &d_opt) override;
        u_mv_opt_info compile_move(Assembler &as, Assembler::Chunk &chunk, const d_mv_opt_info &d_opt) override;
    public:
        explicit IntegerAST(const std::string &filename, code_loc_t token_loc, int64_t num);
    };

    /**
     * Class of AST leaves which represent integer literals.
     */
    class FloatAST : public AST {
        double flp; // Number represented by the literal.

        u_ev_opt_info compile_eval(Assembler &as, Assembler::Chunk &chunk, const d_ev_opt_info &d_opt) override;
        u_mv_opt_info compile_move(Assembler &as, Assembler::Chunk &chunk, const d_mv_opt_info &d_opt) override;
    public:
        explicit FloatAST(const std::string &filename, code_loc_t token_loc, double flp);
    };

    /**
     * Class of AST leaves which represent identifiers.
     */
    class IdentifierAST : public AST {
        std::string name; // Name of the identifier.

        u_ev_opt_info compile_eval(Assembler &as, Assembler::Chunk &chunk, const d_ev_opt_info &d_opt) override;
        u_mv_opt_info compile_move(Assembler &as, Assembler::Chunk &chunk, const d_mv_opt_info &d_opt) override;

        std::string get_identifier() const override;
    public:
        explicit IdentifierAST(const std::string &filename, code_loc_t token_loc, std::string name);
    };

    /**
     * Class of AST nodes which represent operator expressions.
     * These AST nodes have operator's operands as their children.
     */
    class OperatorAST : public AST {
        Operator op; // The operator of this expression.
        ast_ptr left{}, right{}; // Operands of the operator.

        u_ev_opt_info compile_eval(Assembler &as, Assembler::Chunk &chunk, const d_ev_opt_info &d_opt) override;
        u_mv_opt_info compile_move(Assembler &as, Assembler::Chunk &chunk, const d_mv_opt_info &d_opt) override;

        std::pair<AST *, AST *> get_then() const override;

        code_loc_t get_location() const override;
    public:
        OperatorAST(const std::string &filename, code_loc_t token_loc, AST *left, AST *right, Operator op);
    };

    /**
     * Class of AST leaves which represent `nul` literals.
     */
    class NulAST : public AST {
        u_ev_opt_info compile_eval(Assembler &as, Assembler::Chunk &chunk, const d_ev_opt_info &d_opt) override;
        u_mv_opt_info compile_move(Assembler &as, Assembler::Chunk &chunk, const d_mv_opt_info &d_opt) override;
    public:
        NulAST(const std::string &filename, code_loc_t token_loc);
    };

    /**
     * Class of AST leaves which represent void literal.
     */
    class VoidAST : public AST {
        u_ev_opt_info compile_eval(Assembler &as, Assembler::Chunk &chunk, const d_ev_opt_info &d_opt) override;
        u_mv_opt_info compile_move(Assembler &as, Assembler::Chunk &chunk, const d_mv_opt_info &d_opt) override;
    public:
        VoidAST(const std::string &filename, code_loc_t token_loc);
    };

    /**
     * Class of AST nodes which represent bracket expressions.
     * These AST nodes have brackets' sub-expression as their child.
     */
    class BracketAST : public AST {
        Bracket type; // Type of the brackets in this expression.
        ast_ptr child; // The sub-expression which is enclosed by brackets.
        u_ev_opt_info compile_eval(Assembler &as, Assembler::Chunk &chunk, const d_ev_opt_info &d_opt) override;
        u_mv_opt_info compile_move(Assembler &as, Assembler::Chunk &chunk, const d_mv_opt_info &d_opt) override;
    public:
        BracketAST(const std::string &filename, code_loc_t token_loc, AST *child, Bracket type);
    };

    /**
     * Class of AST leaves which represent boolean literals.
     */
    class BooleanAST : public AST {
        bool bln; // Whether the literal represents `yes` value.

        u_ev_opt_info compile_eval(Assembler &as, Assembler::Chunk &chunk, const d_ev_opt_info &d_opt) override;
        u_mv_opt_info compile_move(Assembler &as, Assembler::Chunk &chunk, const d_mv_opt_info &d_opt) override;
    public:
        explicit BooleanAST(const std::string &filename, code_loc_t token_loc, bool bln);
    };

    /**
     * Class of AST leaves which represent string literals.
     */
    class StringAST : public AST {
        std::string str;

        u_ev_opt_info compile_eval(Assembler &as, Assembler::Chunk &chunk, const d_ev_opt_info &d_opt) override;
        u_mv_opt_info compile_move(Assembler &as, Assembler::Chunk &chunk, const d_mv_opt_info &d_opt) override;
    public:
        explicit StringAST(const std::string &filename, code_loc_t token_loc, std::string str);
    };
}

#endif //FUNSCRIPT_AST_HPP
