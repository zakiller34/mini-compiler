#include "parser.h"

#include <memory>
#include <string>
#include <vector>

/// @brief Construct parser, read first token
/// @requires lex is valid Lexer
Parser::Parser(Lexer &lex) : lex_(lex), cur_(lex_.next()) {}

/// @brief Advance to next token
/// @ensures cur_ is updated
void Parser::advance() { cur_ = lex_.next(); }

/// @brief Expect token kind, throw ParseError if mismatch
/// @requires msg describes expected token
void Parser::expect(TokenKind kind, const std::string &msg) {
    if (cur_.kind != kind) {
        throw ParseError("expected " + msg + ", got '" + cur_.text + "'");
    }
    advance();
}

/// @brief Parse full program: expr EOF
/// @ensures result is valid Program
std::unique_ptr<Program> Parser::parse_program() {
    auto body = parse_expr();
    if (cur_.kind != TokenKind::Eof) {
        throw ParseError("expected EOF, got '" + cur_.text + "'");
    }
    return std::make_unique<Program>(std::move(body));
}

/// @brief Parse expr: let binding or additive
/// @ensures result is valid Expr node
std::unique_ptr<Expr> Parser::parse_expr() {
    if (cur_.kind == TokenKind::Let) {
        advance();
        if (cur_.kind != TokenKind::Ident) {
            throw ParseError("expected identifier after let");
        }
        std::string var = cur_.text;
        advance();
        expect(TokenKind::Equals, "'='");
        auto init = parse_additive();
        expect(TokenKind::Semicolon, "';'");
        auto body = parse_expr();
        return std::make_unique<LetExpr>(std::move(var), std::move(init), std::move(body));
    }
    return parse_additive();
}

/// @brief Parse additive: left-assoc + and - over unary (iterative)
/// @ensures result is valid Expr with left-associative +/- chain
std::unique_ptr<Expr> Parser::parse_additive() {
    auto lhs = parse_unary();

    // decreases: remaining tokens
    // invariant: lhs accumulates left-assoc binary exprs
    while (cur_.kind == TokenKind::Plus || cur_.kind == TokenKind::Minus) {
        BinaryOp op = (cur_.kind == TokenKind::Plus) ? BinaryOp::Add : BinaryOp::Sub;
        advance();
        auto rhs = parse_unary();
        lhs = std::make_unique<BinaryExpr>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

/// @brief Parse unary: prefix - chain (iterative, builds from inside out)
/// @ensures result is valid Expr with nested UnaryExpr for each -
std::unique_ptr<Expr> Parser::parse_unary() {
    int neg_count = 0;

    // decreases: remaining '-' tokens
    // invariant: neg_count == number of '-' consumed
    while (cur_.kind == TokenKind::Minus) {
        ++neg_count;
        advance();
    }

    auto expr = parse_primary();

    // decreases: neg_count
    // invariant: expr wrapped with (neg_count - i) negations
    for (int i = 0; i < neg_count; ++i) {
        expr = std::make_unique<UnaryExpr>(UnaryOp::Neg, std::move(expr));
    }
    return expr;
}

/// @brief Parse primary: int, ident, read(), (expr)
/// @ensures result is valid leaf or parenthesized Expr
std::unique_ptr<Expr> Parser::parse_primary() {
    switch (cur_.kind) {
    case TokenKind::IntLit: {
        int64_t val = cur_.int_val;
        advance();
        return std::make_unique<IntExpr>(val);
    }
    case TokenKind::Ident: {
        std::string name = cur_.text;
        advance();
        return std::make_unique<VarExpr>(std::move(name));
    }
    case TokenKind::Read: {
        advance();
        expect(TokenKind::LParen, "'('");
        expect(TokenKind::RParen, "')'");
        return std::make_unique<ReadExpr>();
    }
    case TokenKind::LParen: {
        advance();
        auto inner = parse_expr();
        expect(TokenKind::RParen, "')'");
        return std::move(inner);
    }
    default:
        throw ParseError("unexpected token: '" + cur_.text + "'");
    }
}
