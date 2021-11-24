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
        PLAIN,
        PLAIN_R
    };

    struct Token {
        enum Type {
            UNKNOWN = 0,
            ID,
            INTEGER,
            OPERATOR,
            BRACKET
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

    static const std::map<std::wstring, Bracket> BRACKET_KEYWORDS{
            {L"(", Bracket::PLAIN},
            {L")", Bracket::PLAIN_R},
    };

    struct OperatorMeta {
        int order;
        bool left;
        const wchar_t *name;
    };

    static const std::map<Operator, OperatorMeta> OPERATORS{
            {Operator::TIMES,    {1,  true,  L"times"}},
            {Operator::DIVIDE,    {1,  true,  L"divide"}},
            {Operator::PLUS,    {2,  true,  L"plus"}},
            {Operator::MINUS,    {2,  true,  L"minus"}},
            {Operator::APPEND,  {9,  true,  L"append"}}, // special
            {Operator::ASSIGN,  {10, false, L"assign"}}, // special
            {Operator::DISCARD, {11, false,  L"discard"}}, // special
    };

    enum class Opcode : char {
        NOP,
        SEP,
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
