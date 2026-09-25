#include "parser.h"

#include <stdexcept>
#include <string>

namespace {

// decodes the contents of a quoted string or character token.
std::string decode_string_literal(const Token &token) {
    const std::string text = token.current_token_string();
    std::string value;
    value.reserve(text.size() - 2);

    for (std::size_t index = 1; index + 1 < text.size(); ++index) {
        if (text[index] != '\\') {
            value += text[index];
            continue;
        }

        ++index;
        switch (text[index]) {
        case 'n':
            value += '\n';
            break;
        case 'r':
            value += '\r';
            break;
        case 't':
            value += '\t';
            break;
        case '0':
            value += '\0';
            break;
        default:
            value += text[index];
            break;
        }
    }

    return value;
}

} // namespace

// parses prefix operators and then delegates to a primary expression.
std::unique_ptr<Expr> Parser::parse_unary() {
    Token tok = peek();
    UnaryOp op;

    switch (tok.kind) {
    case TokenKind::OP_MINUS:
        op = UnaryOp::Negate;
        break;
    case TokenKind::OP_LOGICAL_NOT:
        op = UnaryOp::LogicalNot;
        break;
    case TokenKind::OP_BITWISE_NOT:
        op = UnaryOp::BitwiseNot;
        break;
    case TokenKind::OP_PLUS:
        op = UnaryOp::Positive;
        break;
    case TokenKind::OP_BITWISE_AND: {
        lookahead_.reset(); // consume "&".

        if (peek().kind == TokenKind::KW_MUT) {
            op = UnaryOp::MutReference;
            lookahead_.reset(); // consume "mut".
        } else {
            op = UnaryOp::Reference;
        }

        auto operand = parse_unary();
        if (!operand) {
            return nullptr;
        }

        return std::make_unique<Unary>(tok.location, op, std::move(operand));
    }
    default:
        return parse_primary();
    }

    lookahead_.reset(); // consume the ordinary unary operator.

    auto operand = parse_unary();
    if (!operand) {
        return nullptr;
    }

    return std::make_unique<Unary>(tok.location, op, std::move(operand));
}

// parses literals, names, casts, arrays, struct values, and grouped values.
std::unique_ptr<Expr> Parser::parse_primary() {
    Token tok = peek();
    PrimitiveTypeKind cast_type = PrimitiveTypeKind::Void;
    bool is_cast = false;

    switch (tok.kind) {
    case TokenKind::KW_I32:
        cast_type = PrimitiveTypeKind::i32;
        is_cast = true;
        break;
    case TokenKind::KW_U32:
        cast_type = PrimitiveTypeKind::u32;
        is_cast = true;
        break;
    case TokenKind::KW_USIZE:
        cast_type = PrimitiveTypeKind::usize;
        is_cast = true;
        break;
    case TokenKind::KW_F32:
        cast_type = PrimitiveTypeKind::f32;
        is_cast = true;
        break;
    case TokenKind::KW_F64:
        cast_type = PrimitiveTypeKind::f64;
        is_cast = true;
        break;
    default:
        break;
    }

    if (is_cast) {
        advance();
        expect(TokenKind::PUNC_LPAREN);

        auto operand = parse_expression();
        if (!operand) {
            throw std::runtime_error("Expected expression in cast");
        }

        expect(TokenKind::PUNC_RPAREN);
        return std::make_unique<CastExpr>(tok.location, cast_type,
                                          std::move(operand));
    }

    if (tok.kind == TokenKind::LIT_INT) {
        advance(); // consume the integer literal.
        const std::string text = tok.current_token_string();
        const bool is_binary =
            text.rfind("0b", 0) == 0 || text.rfind("0B", 0) == 0;
        const std::uint64_t value =
            is_binary ? std::stoull(text.substr(2), nullptr, 2)
                      : std::stoull(text, nullptr, 10);
        return std::make_unique<Literal>(tok.location, LiteralKind::Integer,
                                         value);
    } else if (tok.kind == TokenKind::LIT_FLOAT) {
        advance();
        return std::make_unique<Literal>(tok.location, LiteralKind::Float,
                                         std::stod(tok.current_token_string()));
    } else if (tok.kind == TokenKind::LIT_STRING) {
        advance();
        return std::make_unique<Literal>(tok.location, LiteralKind::String,
                                         decode_string_literal(tok));
    } else if (tok.kind == TokenKind::LIT_CHAR) {
        advance();
        const std::string value = decode_string_literal(tok);
        if (value.size() != 1) {
            throw std::runtime_error("Invalid character literal");
        }
        return std::make_unique<Literal>(tok.location, LiteralKind::Char,
                                         value[0]);
    } else if (tok.kind == TokenKind::LIT_TRUE ||
               tok.kind == TokenKind::LIT_FALSE) {
        advance();
        return std::make_unique<Literal>(tok.location, LiteralKind::Boolean,
                                         tok.kind == TokenKind::LIT_TRUE);
    } else if (tok.kind == TokenKind::IDENTIFIER) {
        advance(); // consume the identifier.

        if (check(TokenKind::PUNC_LBRACE)) {
            advance();
            std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;

            if (!check(TokenKind::PUNC_RBRACE)) {
                while (true) {
                    Token field = peek();
                    if (field.kind != TokenKind::IDENTIFIER) {
                        throw std::runtime_error("Expected struct field name");
                    }

                    advance();
                    expect(TokenKind::PUNC_COLON);

                    auto value = parse_expression();
                    if (!value) {
                        throw std::runtime_error("Expected struct field value");
                    }

                    fields.emplace_back(field.current_token_string(),
                                        std::move(value));
                    if (!match(TokenKind::PUNC_COMMA)) {
                        break;
                    }
                }
            }

            expect(TokenKind::PUNC_RBRACE);
            return std::make_unique<StructConstructionExpr>(
                tok.location, tok.current_token_string(), std::move(fields));
        }

        return std::make_unique<Identifier>(tok.location,
                                            tok.current_token_string());
    } else if (tok.kind == TokenKind::PUNC_LPAREN) {
        advance(); // consume '('.

        auto expr = parse_expression();
        if (!expr) {
            throw std::runtime_error("Expected expression after '('");
        }

        expect(TokenKind::PUNC_RPAREN); // expect ')'.
        return std::make_unique<Grp>(tok.location, std::move(expr));
    } else if (tok.kind == TokenKind::PUNC_LBRACKET) {
        advance();
        if (check(TokenKind::PUNC_RBRACKET)) {
            throw std::runtime_error("Empty array expression");
        }

        auto first = parse_expression();
        if (!first) {
            throw std::runtime_error("Expected array element");
        }

        if (match(TokenKind::PUNC_SEMICOLON)) {
            std::vector<std::unique_ptr<Expr>> dimensions;
            auto dimension = parse_expression();
            if (!dimension) {
                throw std::runtime_error("Expected array dimension");
            }

            dimensions.push_back(std::move(dimension));
            while (match(TokenKind::PUNC_COMMA)) {
                auto next_dimension = parse_expression();
                if (!next_dimension) {
                    throw std::runtime_error("Expected array dimension");
                }

                dimensions.push_back(std::move(next_dimension));
            }

            expect(TokenKind::PUNC_RBRACKET);
            return std::make_unique<RepeatArrayExpr>(
                tok.location, std::move(first), std::move(dimensions));
        }

        std::vector<std::unique_ptr<Expr>> elements;
        elements.push_back(std::move(first));
        while (match(TokenKind::PUNC_COMMA)) {
            auto element = parse_expression();
            if (!element) {
                throw std::runtime_error("Expected array element");
            }

            elements.push_back(std::move(element));
        }

        expect(TokenKind::PUNC_RBRACKET);
        return std::make_unique<ArrayExpr>(tok.location, std::move(elements));
    } else {
        // rejects tokens that cannot begin a primary expression.
        throw std::runtime_error("Unexpected token in primary expression");
    }
}
