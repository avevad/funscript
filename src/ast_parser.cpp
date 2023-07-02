#include "ast.hpp"
#include "common.hpp"

namespace funscript {

    bool insert_void_after(Token::Type type) {
        return type == Token::OPERATOR || type == Token::LEFT_BRACKET;
    }

    bool insert_call_after(Token::Type type) {
        return !insert_void_after(type);
    }

    CompilationError::CompilationError(const std::string &filename, const code_loc_t &loc, const std::string &msg) :
            std::runtime_error(msg + " at " + filename + ':' + loc.to_string()) {}

    ast_ptr parse(const std::string &filename, std::vector<Token> tokens) {
        { // Filtering comments out
            std::vector<Token> tokens_new;
            for (const auto &token : tokens) if (token.type != Token::COMMENT) tokens_new.push_back(token);
            tokens = tokens_new;
        }
        // Empty expressions should be treated as void expression `()`
        if (tokens.empty()) return ast_ptr(new VoidAST(filename, {1, 1}));
        // Transformation into reverse Polish notation using https://en.m.wikipedia.org/wiki/Shunting_yard_algorithm
        std::vector<Token> stack; // Operator stack
        std::vector<Token> queue; // Output queue
        for (size_t pos = 0; pos < tokens.size(); pos++) {
            const auto &token = tokens[pos];
            switch (token.type) {
                // These literal-like token types act the same
                case Token::INTEGER:
                case Token::FLOAT:
                case Token::BOOLEAN:
                case Token::STRING:
                case Token::ID: {
                    // We need to insert implicit call operator if the previous token was literal-like token or right bracket
                    // As in `fib 5` or `arr[2][3]`
                    if (pos != 0 && insert_call_after(tokens[pos - 1].type)) {
                        // Call operator has the highest precedence, so we just put it onto the stack
                        stack.emplace_back(Token::OPERATOR, Operator::CALL,
                                           code_loc_t{tokens[pos - 1].location.end, tokens[pos].location.beg});
                    }
                    queue.push_back(token);
                    break;
                }
                case Token::OPERATOR: {
                    // If it is the second consecutive operator or if it occurs right after left bracket, we insert implicit void token
                    // As in `(+5)` or `val, *arr`
                    if (pos == 0 || insert_void_after(tokens[pos - 1].type)) {
                        auto beg = pos == 0 ? code_pos_t{1, 1} : tokens[pos - 1].location.end;
                        queue.emplace_back(Token::VOID, 0,
                                           code_loc_t{beg, tokens[pos].location.beg});
                    }
                    OperatorMeta op1 = get_operators_meta().at(std::get<Operator>(token.data)); // Current operator meta
                    // We pop operators with higher precedence after last bracket onto the output queue because their result is calculated first
                    while (!stack.empty()) {
                        Token top = stack.back();
                        if (top.type != Token::OPERATOR) break; // We found a bracket, so we need to stop
                        OperatorMeta op2 = get_operators_meta().at(std::get<Operator>(top.data));
                        if (op2.order < op1.order || (op2.order == op1.order && op1.left)) {
                            stack.pop_back();
                            queue.push_back(top);
                        } else break;
                    }
                    stack.push_back(token);
                    break;
                }
                case Token::LEFT_BRACKET: {
                    // As it is with literal-like tokens, we need to insert call operator in the same cases
                    // The operator is left-associative before brackets
                    if (pos != 0 && insert_call_after(tokens[pos - 1].type)) {
                        while (!stack.empty()) {
                            Token top = stack.back();
                            if (top.type != Token::OPERATOR) break; // We found a bracket, so we need to stop
                            OperatorMeta op2 = get_operators_meta().at(std::get<Operator>(top.data));
                            if (op2.order == 0) {
                                stack.pop_back();
                                queue.push_back(top);
                            } else break;
                        }
                        stack.emplace_back(Token::OPERATOR, Operator::CALL,
                                           code_loc_t{tokens[pos - 1].location.end, tokens[pos].location.beg});
                    }
                    stack.push_back(token);
                    break;
                }
                case Token::RIGHT_BRACKET: {
                    Bracket br = std::get<Bracket>(token.data);
                    // As it is with operators, we need to insert void token in the same cases
                    if (pos == 0 || insert_void_after(tokens[pos - 1].type)) {
                        auto beg = pos == 0 ? code_pos_t{1, 1} : tokens[pos - 1].location.end;
                        queue.emplace_back(Token::VOID, 0,
                                           code_loc_t{beg, tokens[pos].location.beg});
                    }
                    // We pop any remaining operators inside current bracket onto the output queue
                    while (!stack.empty() && stack.back().type != Token::LEFT_BRACKET) {
                        queue.push_back(stack.back());
                        stack.pop_back();
                    }
                    if (stack.empty()) throw CompilationError(filename, token.location, "unmatched right bracket");
                    auto beg = stack.back().location.beg;
                    if (get<Bracket>(stack.back().data) != br) {
                        throw CompilationError(filename,
                                               code_loc_t{beg, token.location.end},
                                               "brackets do not match");
                    }
                    stack.pop_back();
                    queue.push_back({Token::RIGHT_BRACKET, br, code_loc_t{beg, token.location.end}});
                    break;
                }
                case Token::COMMENT:
                case Token::VOID:
                case Token::UNKNOWN:
                    // These tokens should not occur during parsing
                    assertion_failed("unknown token");
            }
        }
        // Sometimes we need to insert void token right at the end of code, as in `k = 50%`
        if (insert_void_after(tokens.back().type)) {
            queue.push_back({Token::VOID, 0,
                             code_loc_t{tokens.back().location.end, tokens.back().location.end}});
        }
        // Pop any remaining operators onto the output queue while also checking for orphaned brackets
        while (!stack.empty()) {
            if (stack.back().type == Token::LEFT_BRACKET) {
                throw CompilationError(filename, stack.back().location, "unmatched left bracket");
            }
            queue.push_back(stack.back());
            stack.pop_back();
        }
        // Construct AST from RPN using stack of AST parts
        std::vector<AST *> ast;
        for (const Token &token : queue) {
            switch (token.type) {
                case Token::ID: {
                    ast.push_back(new IdentifierAST(filename, token.location, get<std::string>(token.data)));
                    break;
                }
                case Token::INTEGER: {
                    ast.push_back(new IntegerAST(filename, token.location, get<int64_t>(token.data)));
                    break;
                }
                case Token::FLOAT: {
                    ast.push_back(new FloatAST(filename, token.location, get<double>(token.data)));
                    break;
                }
                case Token::OPERATOR: {
                    if (ast.empty()) assertion_failed("missing operand");
                    AST *right = ast.back();
                    ast.pop_back();
                    if (ast.empty()) assertion_failed("missing operand");
                    AST *left = ast.back();
                    ast.pop_back();
                    ast.push_back(new OperatorAST(filename, token.location, left, right, get<Operator>(token.data)));
                    break;
                }
                case Token::LEFT_BRACKET:
                    assertion_failed("left bracket in output queue");
                case Token::RIGHT_BRACKET: {
                    AST *child = ast.back();
                    ast.pop_back();
                    ast.push_back(new BracketAST(filename, token.location, child, std::get<Bracket>(token.data)));
                    break;
                }
                case Token::VOID: {
                    ast.push_back(new VoidAST(filename, token.location));
                    break;
                }
                case Token::UNKNOWN: {
                    assertion_failed("unknown token in output queue");
                }
                case Token::BOOLEAN: {
                    ast.push_back(new BooleanAST(filename, token.location, get<bool>(token.data)));
                    break;
                }
                case Token::STRING: {
                    ast.push_back(new StringAST(filename, token.location, get<std::string>(token.data)));
                    break;
                }
                case Token::COMMENT: {
                    assertion_failed("comment in output queue");
                }
            }
        }
        // There should be only one part of AST at the end of construction
        if (ast.size() != 1) assertion_failed("missing operator");
        return ast_ptr(ast[0]);
    }
}