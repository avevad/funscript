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
    };

    enum class Bracket {
        PLAIN
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
    };

    static const std::map<std::wstring, Bracket> LEFT_BRACKET_KEYWORDS{
            {L"(", Bracket::PLAIN},
    };

    static const std::map<std::wstring, Bracket> RIGHT_BRACKET_KEYWORDS{
            {L")", Bracket::PLAIN},
    };

    struct OperatorMeta {
        int order;
        bool left;
    };

    static const std::map<Operator, OperatorMeta> OPERATORS{
            {Operator::TIMES,   {1,  true}},
            {Operator::DIVIDE,  {1,  true}},
            {Operator::PLUS,    {2,  true}},
            {Operator::MINUS,   {2,  true}},
            {Operator::APPEND,  {9,  true}}, // special
            {Operator::ASSIGN,  {10, false}}, // special
            {Operator::DISCARD, {11, false}}, // special
    };

    enum class Opcode : char {
        NOP,
        SEP,
        NUL,
        INT,
        OP,
        REF,
        VAL,
        MOV,
        DIS,
        END
    };


    class CompilationError : public std::exception {
        const std::string msg;
    public:
        explicit CompilationError(std::string msg) : msg(std::move(msg)) {}

        [[nodiscard]] const char *what() const noexcept override { return msg.c_str(); }
    };
}

#endif //FUNSCRIPT_COMMON_H
