#include "parser.h"

#include <memory>
#include <string>
#include <vector>

Parser::Parser(Lexer &lex) : lex_(lex), cur_(lex_.next()) {}

void Parser::advance() { cur_ = lex_.next(); }

void Parser::expect(TokenKind kind, const std::string &msg) {
    if (cur_.kind != kind) {
        throw ParseError("expected " + msg + ", got '" + cur_.text + "'");
    }
    advance();
}

std::unique_ptr<Program> Parser::parse_program() {
    auto body = parse_expr();
    if (cur_.kind != TokenKind::Eof) {
        throw ParseError("expected EOF, got '" + cur_.text + "'");
    }
    return std::make_unique<Program>(std::move(body));
}

/// @brief Parse expr: let binding or or_expr
std::unique_ptr<Expr> Parser::parse_expr() {
    if (cur_.kind == TokenKind::Let) {
        advance();
        if (cur_.kind != TokenKind::Ident) {
            throw ParseError("expected identifier after let");
        }
        std::string var = cur_.text;
        advance();
        expect(TokenKind::Equals, "'='");
        auto init = parse_or_expr();
        expect(TokenKind::Semicolon, "';'");
        auto body = parse_expr();
        return std::make_unique<LetExpr>(
            std::move(var), std::move(init), std::move(body));
    }
    return parse_or_expr();
}

/// @brief Parse or_expr: and_expr (OR and_expr)*
std::unique_ptr<Expr> Parser::parse_or_expr() {
    auto lhs = parse_and_expr();
    // invariant: lhs accumulates left-assoc or chain
    // decreases: remaining tokens
    while (cur_.kind == TokenKind::Or) {
        advance();
        auto rhs = parse_and_expr();
        lhs = std::make_unique<BinaryExpr>(
            BinaryOp::Or, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

/// @brief Parse and_expr: cmp_expr (AND cmp_expr)*
std::unique_ptr<Expr> Parser::parse_and_expr() {
    auto lhs = parse_cmp_expr();
    // invariant: lhs accumulates left-assoc and chain
    // decreases: remaining tokens
    while (cur_.kind == TokenKind::And) {
        advance();
        auto rhs = parse_cmp_expr();
        lhs = std::make_unique<BinaryExpr>(
            BinaryOp::And, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

/// @brief Parse cmp_expr: additive ((EqEq|Lt|Le|Gt|Ge) additive)?
std::unique_ptr<Expr> Parser::parse_cmp_expr() {
    auto lhs = parse_additive();
    BinaryOp op{};
    bool has_cmp = true;
    switch (cur_.kind) {
    case TokenKind::EqEq: op = BinaryOp::Eq; break;
    case TokenKind::Lt: op = BinaryOp::Lt; break;
    case TokenKind::Le: op = BinaryOp::Le; break;
    case TokenKind::Gt: op = BinaryOp::Gt; break;
    case TokenKind::Ge: op = BinaryOp::Ge; break;
    default: has_cmp = false; break;
    }
    if (has_cmp) {
        advance();
        auto rhs = parse_additive();
        return std::make_unique<BinaryExpr>(
            op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

/// @brief Parse additive: left-assoc + and - over unary
std::unique_ptr<Expr> Parser::parse_additive() {
    auto lhs = parse_unary();
    // invariant: lhs accumulates left-assoc +/- chain
    // decreases: remaining tokens
    while (cur_.kind == TokenKind::Plus || cur_.kind == TokenKind::Minus) {
        BinaryOp op = (cur_.kind == TokenKind::Plus)
                          ? BinaryOp::Add : BinaryOp::Sub;
        advance();
        auto rhs = parse_unary();
        lhs = std::make_unique<BinaryExpr>(
            op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

/// @brief Parse unary: prefix -/not chain (iterative)
std::unique_ptr<Expr> Parser::parse_unary() {
    std::vector<UnaryOp> ops;
    // invariant: ops has all prefix ops consumed
    // decreases: remaining tokens
    while (cur_.kind == TokenKind::Minus || cur_.kind == TokenKind::Not) {
        ops.push_back(cur_.kind == TokenKind::Minus
                          ? UnaryOp::Neg : UnaryOp::Not);
        advance();
    }
    auto expr = parse_primary();
    // invariant: expr wrapped with ops[i+1..] applied
    // decreases: i
    for (int i = static_cast<int>(ops.size()) - 1; i >= 0; --i) {
        expr = std::make_unique<UnaryExpr>(ops[i], std::move(expr));
    }
    return expr;
}

/// @brief Parse primary: int, bool, ident, read(), if, parens
std::unique_ptr<Expr> Parser::parse_primary() {
    switch (cur_.kind) {
    case TokenKind::IntLit: {
        int64_t val = cur_.int_val;
        advance();
        return std::make_unique<IntExpr>(val);
    }
    case TokenKind::True:
        advance();
        return std::make_unique<BoolExpr>(true);
    case TokenKind::False:
        advance();
        return std::make_unique<BoolExpr>(false);
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
    case TokenKind::If: {
        advance();
        expect(TokenKind::LParen, "'('");
        auto cond = parse_expr();
        expect(TokenKind::RParen, "')'");
        expect(TokenKind::LBrace, "'{'");
        auto then_br = parse_expr();
        expect(TokenKind::RBrace, "'}'");
        expect(TokenKind::Else, "'else'");
        expect(TokenKind::LBrace, "'{'");
        auto else_br = parse_expr();
        expect(TokenKind::RBrace, "'}'");
        return std::make_unique<IfExpr>(
            std::move(cond), std::move(then_br), std::move(else_br));
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
