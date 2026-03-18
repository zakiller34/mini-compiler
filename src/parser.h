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

/// @brief Hand-written recursive-descent parser for L_While
/// Grammar:
///   program  → expr EOF
///   expr     → LET IDENT '=' or_expr ';' expr | or_expr
///   or_expr  → and_expr (OR and_expr)*
///   and_expr → cmp_expr (AND cmp_expr)*
///   cmp_expr → additive ((EQ|LT|LE|GT|GE) additive)?
///   additive → additive (+|-) unary | unary
///   unary    → NOT unary | '-' unary | primary
///   primary  → INT | IDENT | TRUE | FALSE | READ '(' ')'
///            | IF '(' expr ')' '{' expr '}' ELSE '{' expr '}'
///            | WHILE '(' expr ')' '{' expr '}'
///            | BEGIN '{' expr (';' expr)* '}'
///            | SET '!' IDENT expr
///            | VECTOR '(' expr (',' expr)* ')'
///            | LENGTH '(' expr ')'
///            | VOID
///            | '(' expr ')'
/// postfix    → primary ('[' INT ']' ('=' expr)? )*
class Parser {
  public:
    explicit Parser(Lexer &lex);
    std::unique_ptr<Program> parse_program();

  private:
    Lexer &lex_;
    Token cur_;

    void advance();
    void expect(TokenKind kind, const std::string &msg);

    std::unique_ptr<Expr> parse_expr();
    std::unique_ptr<Expr> parse_or_expr();
    std::unique_ptr<Expr> parse_and_expr();
    std::unique_ptr<Expr> parse_cmp_expr();
    std::unique_ptr<Expr> parse_additive();
    std::unique_ptr<Expr> parse_unary();
    std::unique_ptr<Expr> parse_postfix();
    std::unique_ptr<Expr> parse_primary();
};
