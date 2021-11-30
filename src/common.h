//
// Created by avevad on 11/20/21.
//

#ifndef FUNSCRIPT_COMMON_H
#define FUNSCRIPT_COMMON_H

#include <utility>
#include <variant>
#include <string>
#include <map>

namespace funscript {

#define M_VERSION_MAJOR 0
#define M_VERSION_MINOR 1
#define M_STR(S) #S
#define M_TO_STR(X) M_STR(X)

    const static constexpr size_t VERSION_MAJOR = M_VERSION_MAJOR;
    const static constexpr size_t VERSION_MINOR = M_VERSION_MINOR;
    const static constexpr char *VERSION = "Funscript " M_TO_STR(M_VERSION_MAJOR) "." M_TO_STR(M_VERSION_MINOR);

#undef M_VERSION_MAJOR
#undef M_VERSION_MINOR
#undef M_STR
#undef M_TO_STR

    enum class Operator : char {
        TIMES,
        DIVIDE,
        PLUS,
        MINUS,
        ASSIGN,
        APPEND,
        DISCARD,
        CALL,
        LAMBDA
    };

    enum class Bracket {
        PLAIN, CURLY
    };

    struct Token {
        enum Type {
            UNKNOWN = 0,
            ID,
            INTEGER,
            OPERATOR,
            LEFT_BRACKET,
            RIGHT_BRACKET,
            NUL,
            VOID, // special
        };
        using Data = std::variant<Operator, Bracket, int64_t, std::wstring>;
        Type type;
        Data data;
    };

    static const std::map<std::wstring, Operator> OPERATOR_KEYWORDS{
            {L"*", Operator::TIMES},
            {L"/", Operator::DIVIDE},
            {L"+", Operator::PLUS},
            {L"-", Operator::MINUS},
            {L"=", Operator::ASSIGN},
            {L",", Operator::APPEND},
            {L";", Operator::DISCARD},
            {L":", Operator::LAMBDA},
    };

    static const std::map<std::wstring, Bracket> LEFT_BRACKET_KEYWORDS{
            {L"(", Bracket::PLAIN},
            {L"{", Bracket::CURLY},
    };

    static const std::map<std::wstring, Bracket> RIGHT_BRACKET_KEYWORDS{
            {L")", Bracket::PLAIN},
            {L"}", Bracket::CURLY},
    };

    struct OperatorMeta {
        int order;
        bool left;
    };

    static const std::map<Operator, OperatorMeta> OPERATORS{
            {Operator::CALL,    {0,  false}}, // special
            {Operator::TIMES,   {1,  true}},
            {Operator::DIVIDE,  {1,  true}},
            {Operator::PLUS,    {2,  true}},
            {Operator::MINUS,   {2,  true}},
            {Operator::LAMBDA,  {8,  false}}, // special
            {Operator::APPEND,  {9,  false}}, // special
            {Operator::ASSIGN,  {10, false}}, // special
            {Operator::DISCARD, {11, false}}, // special
    };

    enum class Opcode : char {
        NOP, // do nothing
        SEP, // push a separator
        NUL, // push NUL
        INT, // push an integer
        OP,  // make an operator call
        REF, // push a variable reference
        VAL, // push a variable value
        MOV, // assign values
        DIS, // discard values until the separator
        END, // finish execution
        NS,  // push new scope
        DS,  // pop current scope
        FUN, // create function
        MVD, // move and discard
        TAB, // push current scope table
    };


    class CompilationError : public std::exception {
        const std::string msg;
    public:
        explicit CompilationError(std::string msg) : msg(std::move(msg)) {}

        [[nodiscard]] const char *what() const noexcept override { return msg.c_str(); }
    };
}

#endif //FUNSCRIPT_COMMON_H
