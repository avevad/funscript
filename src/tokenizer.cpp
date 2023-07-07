#include "tokenizer.hpp"

#include <algorithm>
#include <stdexcept>

namespace funscript {

    funscript::TokenAutomaton::TokenAutomaton() {
        // Populate the collection of all possible keywords
        kws_part.reserve(get_keyword_mapping().size());
        for (const auto &[kw, str] : get_keyword_mapping()) kws_part.push_back(kw);
    }

    void funscript::TokenAutomaton::append(char c) {
        if (str_part) {
            if (str_end) str_part = false; // There can be no symbols after the end of the literal
            else {
                if (len == 0) str_part = c == '\''; // Every string literal begins with a quote
                else if (c == '\'') str_end = true; // And it also ends with a quote
            }
        }
        // Every identifier consists of alphanumeric characters and underscores and must not start with a digit
        if (len == 0) id_part = std::isalpha(c) || c == '_';
        else if (id_part) id_part = std::isalnum(c) || c == '_';
        // Every integer literal is just digits
        if (int_part) int_part = std::isdigit(c);
        // Every hexadecimal literal begins with `0x` and contains digits or letters a-z or A-Z
        if (hex_part) {
            if (len == 0) hex_part = c == '0';
            else if (len == 1) hex_part = c == 'x';
            else hex_part = std::isdigit(c) || ('a' <= c && c <= 'z' || 'A' <= c && c <= 'Z');
        }
        // Every floating-point literal is digits with maximum one dot (not in the beginning)
        if (flp_part) {
            if (std::isdigit(c)) {
                flp_part = true;
            } else if (c == '.') {
                if (len == 0 || flp_dot) flp_part = false;
                else flp_dot = true;
            } else flp_part = false;
        }
        // Every line comment starts with a number sign and ends at the newline
        if (line_comm_part) {
            if (len == 0) line_comm_part = c == '#';
            else if (len == 1) line_comm_part = c != '[' && c != '\n';
            else line_comm_part = c != '\n';
        }
        // Block comments start with `#[`
        if (block_comm_part) {
            if (len == 0) block_comm_part = c == '#';
            else if (len == 1) block_comm_part = c == '[';
            else if (!block_comm_end_bracket) { // Block comments end with `]#'
                block_comm_end_bracket = c == ']';
            } else if (!block_comm_end_sign) {
                block_comm_end_sign = c == '#';
            } else block_comm_part = false;
        }
        // Iterate through all of currently possible keywords and remove those which doesn't match the new character
        decltype(kws_part) kws_part_new;
        kws_part_new.reserve(kws_part.size());
        for (const auto &kw : kws_part) {
            if (len >= get_keyword_mapping().at(kw).size() || get_keyword_mapping().at(kw).at(len) != c) continue;
            kws_part_new.push_back(kw);
        }
        kws_part = kws_part_new;

        len++;
    }

    bool funscript::TokenAutomaton::is_valid() const {
        return str_part || id_part || int_part || hex_part || flp_part || line_comm_part || block_comm_part ||
               !kws_part.empty();
    }

    namespace {
        static bool is_valid_id(const std::string &id_str) {
            return (std::isalpha(id_str[0]) || id_str[0] == '_') &&
                   std::all_of(id_str.begin(), id_str.end(),
                               [](auto c) -> bool { return std::isalnum(c) || c == '_'; });
        }

        static uint8_t get_hex_digit(const std::string &filename, const code_loc_t &loc, char c) {
            if ('0' <= c && c <= '9') return c - '0';
            if ('a' <= c && c <= 'f') return 10 + c - 'a';
            if ('A' <= c && c <= 'F') return 10 + c - 'A';
            throw CompilationError(filename, loc, "invalid escape sequence");
        }
    }

    Token get_token(const std::string &filename, const code_loc_t &loc, const std::string &token_str) {
        // Token cannot be empty
        if (token_str.empty()) return {Token::UNKNOWN};
        // Token can be a string literal enclosed in quotes
        if (token_str.size() >= 2 && token_str.starts_with('\'') && token_str.ends_with('\'')) {
            std::string result;
            for (size_t pos = 1; pos + 1 < token_str.size();) {
                if (token_str[pos] == '\\') {
                    if (pos + 1 == token_str.size() - 1)
                        throw CompilationError(filename, loc, "invalid escape sequence");
                    else if (token_str[pos + 1] == '\\') {
                        result += '\\';
                        pos += 2;
                    } else if (token_str[pos + 1] == 'x') {
                        if (pos + 3 >= token_str.size() - 1) {
                            throw CompilationError(filename, loc, "invalid escape sequence");
                        }
                        char c = char((get_hex_digit(filename, loc, token_str[pos + 2]) << 4) +
                                      get_hex_digit(filename, loc, token_str[pos + 3]));
                        result += c;
                        pos += 4;
                    }
                } else result += token_str[pos++];
            }
            return {Token::STRING, result};
        }
        // Token can be a keyword of different types
        if (get_inverse_keyword_mapping().contains(token_str)) {
            Keyword keyword = get_inverse_keyword_mapping().at(token_str);
            // Literal keywords
            if (keyword == Keyword::YES) return {Token::BOOLEAN, true};
            if (keyword == Keyword::NO) return {Token::BOOLEAN, false};
            if (keyword == Keyword::NAN) return {Token::FLOAT, nan()};
            if (keyword == Keyword::INF) return {Token::FLOAT, inf()};
            // Operator keywords
            if (get_operator_keyword_mapping().contains(keyword)) {
                return {Token::OPERATOR, get_operator_keyword_mapping().at(keyword)};
            }
            // Bracket keywords
            if (get_left_bracket_keyword_mapping().contains(keyword)) {
                return {Token::LEFT_BRACKET, get_left_bracket_keyword_mapping().at(keyword)};
            }
            if (get_right_bracket_keyword_mapping().contains(keyword)) {
                return {Token::RIGHT_BRACKET, get_right_bracket_keyword_mapping().at(keyword)};
            }
            // No more known keyword kinds
            return {Token::UNKNOWN};
        }
        // Token can be an integer literal
        if (std::all_of(token_str.begin(), token_str.end(), isdigit)) return {Token::INTEGER, std::stoull(token_str)};
        // Token can be a hexadecimal integer literal
        if (token_str.starts_with("0x")) {
            if (std::all_of(token_str.begin() + 2, token_str.end(), [](char c) -> bool {
                return std::isdigit(c) || ('A' <= c && c <= 'Z' || 'a' <= c && c <= 'z');
            })) {
                return {Token::INTEGER, std::stoull(token_str, nullptr, 16)};
            }
        }
        // Token can be a floating-point literal
        if (std::all_of(token_str.begin(), token_str.end(), [](char c) -> bool { return isdigit(c) || c == '.'; })) {
            if (std::count(token_str.begin(), token_str.end(), '.') <= 1) {
                return {Token::FLOAT, std::stod(token_str)};
            }
        }
        // Token can be an identifier
        if (is_valid_id(token_str)) return {Token::ID, token_str};
        // Token can be a block comment
        if (token_str.starts_with("#[") && token_str.ends_with("]#")) return {Token::COMMENT};
        // Token can be a line comment
        if (token_str[0] == '#') return {Token::COMMENT};
        // No more known token types
        return {Token::UNKNOWN};
    }

    void tokenize(const std::string &filename, const std::string &code, const std::function<void(Token)> &cb) {
        size_t left = 0; // Position of leftmost character of current token
        code_pos_t left_pos = {1, 1};
        // Skip whitespaces at the beginning
        while (left < code.length() && isspace(code[left])) {
            if (code[left] == '\n') left_pos = {left_pos.row + 1, 1};
            else left_pos.col++;
            left++;
        }
        while (left < code.length()) {
            size_t right = left; // Position of rightmost character of current token + 1
            code_pos_t right_pos = left_pos;
            TokenAutomaton automaton;
            // Add characters one by one until the token is no longer valid
            while (automaton.is_valid()) {
                if (right == code.length()) {
                    right++;
                    break;
                }
                automaton.append(code[right++]);
            }
            right--; // Now it points to the first character which doesn't belong to the current token
            for (size_t right1 = left; right1 != right; right1++) { // Update right_pos by scanning through the token
                if (code[right1] == '\n') right_pos = {right_pos.row + 1, 1};
                else right_pos.col++;
            }
            // Handle current token
            Token token = get_token(filename, {left_pos, right_pos}, code.substr(left, right - left));
            if (!token.type) throw CompilationError(filename, {left_pos, right_pos}, "unknown token");
            token.location = {left_pos, right_pos};
            cb(token);
            // Move onto the next one
            left = right;
            left_pos = right_pos;
            // Skip whitespaces again
            while (left < code.length() && isspace(code[left])) {
                if (code[left] == '\n') left_pos = {left_pos.row + 1, 1};
                else left_pos.col++;
                left++;
            }
        }
    }

}