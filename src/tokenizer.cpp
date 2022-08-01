//
// Created by avevad on 11/20/21.
//

#include "tokenizer.h"
#include "common.h"

#include <functional>
#include <stdexcept>
#include <cwctype>


namespace funscript {

    TokenAutomaton::TokenAutomaton(AllocatorWrapper<fchar> alloc) : alloc(alloc) {
        ops_part.reserve(OPERATOR_KEYWORDS.size());
        for (const auto &[kw, op]: OPERATOR_KEYWORDS) ops_part.push_back(ascii2fstring(kw, alloc));
    }

    void TokenAutomaton::append(fchar ch) {
        if (len == 0) {
            left_bracket_part = LEFT_BRACKET_KEYWORDS.contains(fchar2ascii(ch));
            right_bracket_part = RIGHT_BRACKET_KEYWORDS.contains(fchar2ascii(ch));
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
        std::vector<fstring> ops_part_new;
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

    bool is_valid_id(const fstring &token, size_t offset = 0) {
        return (iswalpha(token[offset]) || token[offset] == L'_') &&
               std::all_of(token.begin() + offset, token.end(), [](fchar c) { return iswalnum(c) || c == L'_'; });
    }

    Token get_token(const fstring &token) {
        if (token.empty()) return {Token::UNKNOWN};
        if (fstring2ascii(token) == NUL_KW) return {Token::NUL, 0};
        if (fstring2ascii(token) == YES_KW) return {Token::BOOLEAN, true};
        if (fstring2ascii(token) == NO_KW) return {Token::BOOLEAN, false};
        if (OPERATOR_KEYWORDS.contains(fstring2ascii(token))) {
            return {Token::OPERATOR, OPERATOR_KEYWORDS.at(fstring2ascii(token))};
        }
        if (token.length() == 1 && LEFT_BRACKET_KEYWORDS.contains(fchar2ascii(token[0]))) {
            return {Token::LEFT_BRACKET, LEFT_BRACKET_KEYWORDS.at(fchar2ascii(token[0]))};
        }
        if (token.length() == 1 && RIGHT_BRACKET_KEYWORDS.contains(fchar2ascii(token[0]))) {
            return {Token::RIGHT_BRACKET, RIGHT_BRACKET_KEYWORDS.at(fchar2ascii(token[0]))};
        }
        if (std::all_of(token.begin(), token.end(), iswdigit)) {
            return {Token::INTEGER, std::stoll(fstring2ascii(token))};
        }
        if (is_valid_id(token)) return {Token::ID, token};
        if (fstring2ascii(token) == "." || token[0] == '.' && is_valid_id(token, 1))
            return {Token::INDEX, fstring(token.begin() + 1, token.end(), token.get_allocator())};
        return {Token::UNKNOWN};
    }

    void tokenize(const fstring &code, const std::function<void(Token)> &cb) {
        size_t l = 0;
        while (l < code.length() && iswspace(code[l])) l++;
        while (l < code.length()) {
            size_t r = l;
            TokenAutomaton automaton(code.get_allocator());
            while (automaton.is_valid()) {
                if (r == code.length()) {
                    r++;
                    break;
                }
                automaton.append(code[r++]);
            }
            r--;
            Token token = get_token(fstring(code.begin() + l, code.begin() + r, code.get_allocator()));
            if (token.type == Token::UNKNOWN)
                throw CompilationError("invalid token");
            cb(token);
            l = r;
            while (l < code.length() && iswspace(code[l])) l++;
        }
    }
}
