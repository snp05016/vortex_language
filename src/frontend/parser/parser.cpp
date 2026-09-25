#include "parser.h"

#include <iostream>

// constructs a parser for a source buffer.
Parser::Parser(const char *source, std::size_t length) : lexer(source, length) {
}

// prints each token until the lexer reaches the end of the source.
void Parser::parse() {
    while (true) {
        Token token = lexer.next_token();
        if (token.kind == TokenKind::EOF_TOKEN) {
            break;
        }

        // statement and expression dispatch will be added here later.
        std::cout << "Token kind: " << token.current_token_string()
                  << ", Location: " << token.location.start << "-"
                  << (token.location.start + token.location.length)
                  << std::endl;
    }
}

// parses an expression through the binary-expression entry point.
std::unique_ptr<Expr> Parser::parse_expression() {
    return parse_binary_expression(0);
}

// parses the current expression with the requested precedence floor.
std::unique_ptr<Expr> Parser::parse_binary_expression(int min_precedence) {
    (void)min_precedence;
    return parse_unary();
}
