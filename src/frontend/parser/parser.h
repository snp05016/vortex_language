#pragma once // this is use to prevent inclusion of the same header file
#include "../ast.h"
#include "../lexer.h"
#include "../token.h"
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

class Parser {
  public:
    Parser(const char *source,
           std::size_t length); // constructs a parser for a source buffer.
    void parse();               // prints the tokens in the source buffer.
    std::optional<Token> lookahead_;
    std::unique_ptr<Expr> parse_expression();
    std::unique_ptr<Expr> parse_binary_expression(
        int min_precedence); // parses an expression at a precedence floor.
    std::unique_ptr<Stmt> parse_statement();
    std::unique_ptr<Decl> parse_declaration();
    std::unique_ptr<Type> parse_type();
    std::unique_ptr<Expr> parse_unary();
    std::unique_ptr<Expr> parse_primary();

  private:
    Token peek(); // returns the next token without consuming it.
    Lexer lexer;

    bool check(TokenKind kind) {
        Token tok = peek();
        return tok.kind == kind;
    }

    bool match(TokenKind kind) {
        if (check(kind)) {
            lookahead_.reset(); // consume the token.
            return true;
        }
        return false;
    }

    void advance() {
        lookahead_.reset();
    } // consumes the current token.

    void expect(TokenKind kind) {
        if (!match(kind)) {
            // reports a missing token at the current parser position.
            throw std::runtime_error("Expected token not found");
        }
    }
};

static std::unordered_map<TokenKind, int> operator_precedence = {
    {TokenKind::OP_LOGICAL_OR, 1}, {TokenKind::OP_LOGICAL_AND, 2},
    {TokenKind::OP_EQUAL, 3},      {TokenKind::OP_NOT_EQUAL, 3},
    {TokenKind::OP_LESS, 4},       {TokenKind::OP_GREATER, 4},
    {TokenKind::OP_LESS_EQUAL, 4}, {TokenKind::OP_GREATER_EQUAL, 4},
    {TokenKind::OP_PLUS, 5},       {TokenKind::OP_MINUS, 5},
    {TokenKind::OP_MULTIPLY, 6},   {TokenKind::OP_DIVIDE, 6},
    {TokenKind::OP_MODULO, 6},
};

inline Token Parser::peek() {
    if (!lookahead_) {
        lookahead_ = lexer.next_token();
    }
    return *lookahead_;
}
