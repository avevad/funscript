#ifndef FUNSCRIPT_TOKENIZER_H
#define FUNSCRIPT_TOKENIZER_H

#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <functional>

#include "common.h"

namespace funscript {

    /**
     * Enumeration of names of special character combinations.
     */
    enum class Keyword {
        ASTERISK, SLASH, PLUS, MINUS, EQUAL_SIGN, DOT, COMMA, SEMICOLON, COLON, PERCENT, DOUBLE_EQUAL_SIGN, NOT_EQUAL,
        EXCLAMATION_MARK, LESS_SIGN, GREATER_SIGN, LESS_EQUAL_SIGN, GREATER_EQUAL_SIGN, ARROW, QUESTION_MARK,
        LEFT_PLAIN_BRACKET, RIGHT_PLAIN_BRACKET, LEFT_CURLY_BRACKET, RIGHT_CURLY_BRACKET,

        THEN, ELSE, UNTIL, DO, YES, NO, NUL
    };

    /**
     * @return The mapping from keywords to their character combinations.
     */
    static const std::unordered_map<Keyword, std::string> &get_keyword_mapping() {
        static const std::unordered_map<Keyword, std::string> KEYWORD_STRINGS{
                {Keyword::ASTERISK,            "*"},
                {Keyword::SLASH,               "/"},
                {Keyword::PLUS,                "+"},
                {Keyword::MINUS,               "-"},
                {Keyword::EQUAL_SIGN,          "="},
                {Keyword::DOT,                 "."},
                {Keyword::COMMA,               ","},
                {Keyword::SEMICOLON,           ";"},
                {Keyword::COLON,               ":"},
                {Keyword::PERCENT,             "%"},
                {Keyword::DOUBLE_EQUAL_SIGN,   "=="},
                {Keyword::NOT_EQUAL,           "!="},
                {Keyword::EXCLAMATION_MARK,    "!"},
                {Keyword::LESS_SIGN,           "<"},
                {Keyword::GREATER_SIGN,        ">"},
                {Keyword::LESS_EQUAL_SIGN,     "<="},
                {Keyword::GREATER_EQUAL_SIGN,  ">="},
                {Keyword::ARROW,               "=>"},
                {Keyword::QUESTION_MARK,       "?"},
                {Keyword::LEFT_PLAIN_BRACKET,  "("},
                {Keyword::RIGHT_PLAIN_BRACKET, ")"},
                {Keyword::LEFT_CURLY_BRACKET,  "{"},
                {Keyword::RIGHT_CURLY_BRACKET, "}"},

                {Keyword::THEN,                "then"},
                {Keyword::ELSE,                "else"},
                {Keyword::UNTIL,               "until"},
                {Keyword::DO,                  "do"},
                {Keyword::YES,                 "yes"},
                {Keyword::NO,                  "no"},
                {Keyword::NUL,                 "nul"},
        };
        return KEYWORD_STRINGS;
    }

    namespace {
        template<typename K, typename V>
        std::unordered_map<V, K> invert_unordered_map(const std::unordered_map<K, V> &map) {
            std::unordered_map<V, K> inv_map;
            inv_map.reserve(map.size());
            for (const auto &[k, v] : map) inv_map.emplace(v, k);
            return inv_map;
        }
    }

    static const std::unordered_map<std::string, Keyword> &get_inverse_keyword_mapping() {
        static const std::unordered_map<std::string, Keyword> KEYWORDS = invert_unordered_map(get_keyword_mapping());
        return KEYWORDS;
    }

    /**
     * A helper class which allows effective token parsing by appending single characters from code.
     */
    class TokenAutomaton {
        size_t len = 0; // Current token part length
        bool id_part = true; // Is current token part a prefix of an identifier
        bool int_part = true; // Is current token part a prefix of an integer literal
        bool str_part = true; // Is current token part a prefix of a string literal
        bool str_end = false; // Was the closing quote of the string literal already found
        std::vector<Keyword> kws_part; // Keywords which start with current token part
    public:
        TokenAutomaton();

        /**
         * Appends a single character `c` to the token and updates its state.
         * @param c A character to be appended.
         */
        void append(char c);

        /**
         * Checks if current token is still valid.
         * @return Whether the token is still in valid state.
         */
        [[nodiscard]] bool is_valid() const;
    };

    /**
     * Enumeration of bracket expression types.
     */
    enum class Bracket {
        PLAIN, CURLY
    };

    /**
     * Structure containing info about single token of code.
     */
    struct Token {
        enum Type {
            UNKNOWN = 0,
            ID,
            INTEGER,
            OPERATOR,
            LEFT_BRACKET,
            RIGHT_BRACKET,
            NUL,
            VOID, // Implicitly inserted during parsing.
            BOOLEAN,
            STRING
        };
        using Data = std::variant<Operator, Bracket, int64_t, std::string, bool>;
        Type type;
        Data data;
    };

    /**
     * @return Mapping from keywords to corresponding language operators.
     */
    static const std::unordered_map<Keyword, Operator> &get_operator_keyword_mapping() {
        static const std::unordered_map<Keyword, Operator> OPERATOR_KEYWORDS{
                {Keyword::ASTERISK,           Operator::TIMES},
                {Keyword::SLASH,              Operator::DIVIDE},
                {Keyword::PLUS,               Operator::PLUS},
                {Keyword::MINUS,              Operator::MINUS},
                {Keyword::EQUAL_SIGN,         Operator::ASSIGN},
                {Keyword::COMMA,              Operator::APPEND},
                {Keyword::SEMICOLON,          Operator::DISCARD},
                {Keyword::COLON,              Operator::LAMBDA},
                {Keyword::DOT,                Operator::INDEX},
                {Keyword::PERCENT,            Operator::MODULO},
                {Keyword::DOUBLE_EQUAL_SIGN,  Operator::EQUALS},
                {Keyword::NOT_EQUAL,          Operator::DIFFERS},
                {Keyword::EXCLAMATION_MARK,   Operator::NOT},
                {Keyword::LESS_SIGN,          Operator::LESS},
                {Keyword::GREATER_SIGN,       Operator::GREATER},
                {Keyword::LESS_EQUAL_SIGN,    Operator::LESS_EQUAL},
                {Keyword::GREATER_EQUAL_SIGN, Operator::GREATER_EQUAL},
                {Keyword::ARROW,              Operator::THEN},
                {Keyword::THEN,               Operator::THEN},
                {Keyword::QUESTION_MARK,      Operator::ELSE},
                {Keyword::ELSE,               Operator::ELSE},
                {Keyword::UNTIL,              Operator::UNTIL},
                {Keyword::DO,                 Operator::DO}
        };
        return OPERATOR_KEYWORDS;
    }

    /**
     * @return Mapping from keywords to corresponding left bracket types.
     */
    static const std::unordered_map<Keyword, Bracket> &get_left_bracket_keyword_mapping() {
        static const std::unordered_map<Keyword, Bracket> LEFT_BRACKET_KEYWORDS{
                {Keyword::LEFT_PLAIN_BRACKET, Bracket::PLAIN},
                {Keyword::LEFT_CURLY_BRACKET, Bracket::CURLY}
        };
        return LEFT_BRACKET_KEYWORDS;
    }

    /**
     * @return Mapping from keywords to corresponding right bracket types.
     */
    static const std::unordered_map<Keyword, Bracket> &get_right_bracket_keyword_mapping() {
        static const std::unordered_map<Keyword, Bracket> RIGHT_BRACKET_KEYWORDS{
                {Keyword::RIGHT_PLAIN_BRACKET, Bracket::PLAIN},
                {Keyword::RIGHT_CURLY_BRACKET, Bracket::CURLY}
        };
        return RIGHT_BRACKET_KEYWORDS;
    }

    /**
     * Parses a single token from its string.
     * @param token_str A string to parse.
     * @return Parsed token info.
     */
    Token get_token(const std::string &token_str);

    /**
     * Converts funscript code into stream of tokens.
     * @param code Code to tokenize.
     * @param cb Callback function to call for every parsed token.
     */
    void tokenize(const std::string &code, const std::function<void(Token)> &cb);

    class CodeReadingError : public std::runtime_error {
    public:
        explicit CodeReadingError(const std::string &msg) : std::runtime_error(msg) {}
    };
}

#endif //FUNSCRIPT_TOKENIZER_H
