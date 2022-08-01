//
// Created by avevad on 11/20/21.
//

#ifndef FUNSCRIPT_TOKENIZER_H
#define FUNSCRIPT_TOKENIZER_H

#include "common.h"

#include <functional>

namespace funscript {

    static const std::map<std::string, Operator> OPERATOR_KEYWORDS{
            {"*",     Operator::TIMES},
            {"/",     Operator::DIVIDE},
            {"+",     Operator::PLUS},
            {"-",     Operator::MINUS},
            {"=",     Operator::ASSIGN},
            {",",     Operator::APPEND},
            {";",     Operator::DISCARD},
            {":",     Operator::LAMBDA},
            {"%",     Operator::MODULO},
            {"==",    Operator::EQUALS},
            {"!=",    Operator::DIFFERS},
            {"!",     Operator::NOT},
            {"<",     Operator::LESS},
            {">",     Operator::GREATER},
            {"<=",    Operator::LESS_EQUAL},
            {">=",    Operator::GREATER_EQUAL},
            {"=>",    Operator::THEN},
            {"then",  Operator::THEN},
            {"?",     Operator::ELSE},
            {"else",  Operator::ELSE},
            {"until", Operator::UNTIL},
            {"do",    Operator::DO}
    };

    static const std::map<char, Bracket> LEFT_BRACKET_KEYWORDS{
            {'(', Bracket::PLAIN},
            {'{', Bracket::CURLY},
    };

    static const std::map<char, Bracket> RIGHT_BRACKET_KEYWORDS{
            {')', Bracket::PLAIN},
            {'}', Bracket::CURLY},
    };

    static const std::string NUL_KW = "nul";
    static const std::string YES_KW = "yes";
    static const std::string NO_KW = "no";

    class TokenAutomaton {
        AllocatorWrapper<fchar> alloc;
        size_t len = 0;
        bool nul_part = true;
        bool yes_part = true;
        bool no_part = true;
        bool id_part = true;
        bool int_part = true;
        bool index_part = true;
        bool left_bracket_part = true;
        bool right_bracket_part = true;
        std::vector<fstring> ops_part;
    public:
        TokenAutomaton(AllocatorWrapper<fchar> alloc);

        void append(fchar ch);

        [[nodiscard]] bool is_valid() const;
    };

    bool is_valid_id(const fstring &token, size_t offset);

    Token get_token(const fstring &token);

    void tokenize(const fstring &code, const std::function<void(Token)> &cb);

}

#endif //FUNSCRIPT_TOKENIZER_H
