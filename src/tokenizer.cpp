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
        if (BRACKET_KEYWORDS.contains(token)) return {Token::BRACKET, BRACKET_KEYWORDS.at(token)};
        if (std::all_of(token.begin(), token.end(), iswdigit)) {
            return {Token::INTEGER, std::stoll(token)};
        }
        if (is_valid_id(token)) return {Token::ID, token};
        return {Token::UNKNOWN};
    }

    void tokenize(const std::wstring &code, const std::function<void(Token)> &cb) {
        size_t l = 0;
        for (size_t r = 1; r <= code.size(); r++) {
            std::wstring cur_token = code.substr(l, r - l);
            if (get_token(cur_token).type) continue;

            cur_token.pop_back();
            r--;
            if (cur_token.empty()) {
                if (!iswspace(code[r])) throw CompilationError("invalid token");
            } else cb(get_token(cur_token));
            while (iswspace(code[r])) if (++r == code.size()) return;
            l = r;
        }
        if (l < code.length()) {
            Token token = get_token(code.substr(l));
            if (!token.type) throw CompilationError("invalid token");
            cb(token);
        }
    }
}

#include "tokenizer.h"
