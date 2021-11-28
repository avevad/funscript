//
// Created by avevad on 11/20/21.
//

#include "tokenizer.h"
#include "common.h"

#include <functional>
#include <stdexcept>
#include <cwctype>


namespace funscript {
    bool is_valid_id(const std::wstring &token) {
        return (iswalpha(token[0]) || token[0] == '_') &&
               std::all_of(token.begin(), token.end(), [](wchar_t c) { return iswalnum(c) || c == L'_'; });
    }

    Token get_token(const std::wstring &token) {
        if (token == L"nul") return {Token::NUL, 0};
        if (OPERATOR_KEYWORDS.contains(token)) return {Token::OPERATOR, OPERATOR_KEYWORDS.at(token)};
        if (LEFT_BRACKET_KEYWORDS.contains(token)) return {Token::LEFT_BRACKET, LEFT_BRACKET_KEYWORDS.at(token)};
        if (RIGHT_BRACKET_KEYWORDS.contains(token)) return {Token::RIGHT_BRACKET, RIGHT_BRACKET_KEYWORDS.at(token)};
        if (std::all_of(token.begin(), token.end(), iswdigit)) {
            return {Token::INTEGER, std::stoll(token)};
        }
        if (is_valid_id(token)) return {Token::ID, token};
        return {Token::UNKNOWN};
    }

    void tokenize(const std::wstring &code, const std::function<void(Token)> &cb) {
        size_t l = 0;
        while (l < code.length() && iswspace(code[l])) l++;
        while (l < code.length()) {
            size_t r = l;
            while (r < code.length() && get_token(code.substr(l, r + 1 - l)).type != Token::UNKNOWN) r++;
            if (r == l) throw CompilationError("invalid token");
            cb(get_token(code.substr(l, r - l)));
            l = r;
            while (l < code.length() && iswspace(code[l])) l++;
        }
    }
}
