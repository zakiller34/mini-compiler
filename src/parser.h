#pragma once

#include "ast.h"
#include "lexer.h"

#include <memory>
#include <stdexcept>
#include <string>

/// @brief Parse error exception
class ParseError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// @brief Hand-written recursive-descent parser for L_Var
/// Grammar:
///   program  → expr EOF
///   expr     → LET IDENT '=' additive ';' expr | additive
///   additive → additive '+' unary | additive '-' unary | unary
///   unary    → '-' unary | primary
///   primary  → INT_LIT | IDENT | READ '(' ')' | '(' expr ')'
class Parser {
  public:
    /// @brief Construct parser from lexer
    /// @requires lex is valid Lexer
    explicit Parser(Lexer &lex);

    /// @brief Parse full program
    /// @ensures result is valid Program AST
    std::unique_ptr<Program> parse_program();

  private:
    Lexer &lex_;
    Token cur_;

    /// @brief Advance to next token
    void advance();

    /// @brief Expect current token kind, advance
    /// @requires cur_.kind == kind
    void expect(TokenKind kind, const std::string &msg);

    /// @brief Parse expr (let or additive)
    std::unique_ptr<Expr> parse_expr();

    /// @brief Parse additive (left-assoc + and -)
    std::unique_ptr<Expr> parse_additive();

    /// @brief Parse unary (prefix -)
    std::unique_ptr<Expr> parse_unary();

    /// @brief Parse primary (int, ident, read(), parens)
    std::unique_ptr<Expr> parse_primary();
};
