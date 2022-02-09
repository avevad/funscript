//
// Created by avevad on 11/20/21.
//

#include "tokenizer.h"
#include "common.h"

#include <functional>
#include <stdexcept>
#include <cwctype>


namespace funscript {

    TokenAutomaton::TokenAutomaton() {
        ops_part.reserve(OPERATOR_KEYWORDS.size());
        for (const auto &[kw, op]: OPERATOR_KEYWORDS) ops_part.push_back(kw);
    }

    void TokenAutomaton::append(wchar_t ch) {
        if (len == 0) {
            left_bracket_part = LEFT_BRACKET_KEYWORDS.contains(ch);
            right_bracket_part = RIGHT_BRACKET_KEYWORDS.contains(ch);
        } else left_bracket_part = right_bracket_part = false;
        nul_part &= len < NUL_KW.length() && NUL_KW[len] == ch;
        yes_part &= len < YES_KW.length() && YES_KW[len] == ch;
        no_part &= len < NO_KW.length() && NO_KW[len] == ch;
        if (len == 0) {
            if (id_part) id_part = iswalpha(ch) || ch == L'_';
            if (index_part) index_part = ch == '.';
        } else if (len == 1) {
            if (id_part) id_part = iswalnum(ch) || ch == L'_';
            if (index_part) index_part = iswalpha(ch) || ch == L'_';
        } else {
            if (id_part) id_part = iswalnum(ch) || ch == L'_';
            if (index_part) index_part = iswalnum(ch) || ch == L'_';
        }
        int_part &= std::iswdigit(ch);
        std::vector<std::wstring> ops_part_new;
        ops_part_new.reserve(ops_part.size());
        for (const auto &op: ops_part) {
            if (len < op.length() && op[len] == ch) ops_part_new.push_back(op);
        }
        ops_part = ops_part_new;
        len++;
    }

    bool TokenAutomaton::is_valid() const {
        bool is_valid =
                nul_part ||
                yes_part ||
                no_part ||
                id_part ||
                int_part ||
                index_part ||
                left_bracket_part ||
                right_bracket_part ||
                !ops_part.empty();
        return is_valid;
    }

    bool is_valid_id(const std::wstring &token) {
        return (iswalpha(token[0]) || token[0] == L'_') &&
               std::all_of(token.begin(), token.end(), [](wchar_t c) { return iswalnum(c) || c == L'_'; });
    }

    Token get_token(const std::wstring &token) {
        if (token.empty()) return {Token::UNKNOWN};
        if (token == L"nul") return {Token::NUL, 0};
        if (token == L"yes") return {Token::BOOLEAN, true};
        if (token == L"no") return {Token::BOOLEAN, false};
        if (OPERATOR_KEYWORDS.contains(token)) return {Token::OPERATOR, OPERATOR_KEYWORDS.at(token)};
        if (token.length() == 1 && LEFT_BRACKET_KEYWORDS.contains(token[0])) {
            return {Token::LEFT_BRACKET, LEFT_BRACKET_KEYWORDS.at(token[0])};
        }
        if (token.length() == 1 && RIGHT_BRACKET_KEYWORDS.contains(token[0])) {
            return {Token::RIGHT_BRACKET, RIGHT_BRACKET_KEYWORDS.at(token[0])};
        }
        if (std::all_of(token.begin(), token.end(), iswdigit)) {
            return {Token::INTEGER, std::stoll(token)};
        }
        if (is_valid_id(token)) return {Token::ID, token};
        if (token == L"." || token[0] == '.' && is_valid_id(token.substr(1)))
            return {Token::INDEX, token.substr(1)};
        return {Token::UNKNOWN};
    }

    void tokenize(const std::wstring &code, const std::function<void(Token)> &cb) {
        size_t l = 0;
        while (l < code.length() && iswspace(code[l])) l++;
        while (l < code.length()) {
            size_t r = l;
            TokenAutomaton automaton;
            while (automaton.is_valid()) {
                if (r == code.length()) {
                    r++;
                    break;
                }
                automaton.append(code[r++]);
            }
            r--;
            Token token = get_token(code.substr(l, r - l));
            if (token.type == Token::UNKNOWN)
                throw CompilationError("invalid token");
            cb(token);
            l = r;
            while (l < code.length() && iswspace(code[l])) l++;
        }
    }
}
