#pragma once

#include "ast.h"
#include "lexer.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace mc {

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
///            | LAMBDA '(' params ')' ':' type '{' expr '}'
///            | PROCEDURE_ARITY '(' expr ')'
///            | VOID
///            | '(' expr ')'
/// postfix    → primary ('[' INT ']' ('=' expr)? | '(' args ')')*
///   program  → def* expr EOF
///   def      → FN IDENT '(' params ')' ':' type '{' expr '}'
///   params   → (IDENT ':' type (',' IDENT ':' type)*)?
///   type     → INT_KW | BOOL_KW | VOID | ANY_KW | '(' types ')' '->' type
///   args     → (expr (',' expr)*)?
///
/// L_Any additions (static mode only):
///   primary  → INJECT '(' expr ',' type ')' | PROJECT '(' expr ',' type ')'
///            | (INTEGER_P|BOOLEAN_P|VECTOR_P|PROCEDURE_P|VOID_P) '(' expr ')'
///
/// L_Dyn mode (`dyn == true`, selected by `mc --dyn`): all type annotations
/// are absent and every binder is implicitly `Any`.
///   def      → FN IDENT '(' idents ')' '{' expr '}'
///   primary  → LAMBDA '(' idents ')' '{' expr '}'
///   postfix  → primary ('[' expr ']' ('=' expr)? | '(' args ')')*
class Parser {
  public:
    explicit Parser(Lexer &lex, bool dyn = false);
    std::unique_ptr<Program> parse_program();

  private:
    Lexer &lex_;
    Token cur_;
    bool dyn_;

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
    TypePtr parse_type();
    TypePtr parse_fun_type();
    TypePtr parse_annotation();
    std::vector<std::pair<std::string, TypePtr>> parse_params();
    DefNode parse_def();
    std::unique_ptr<Expr> parse_cast();
    std::unique_ptr<Expr> parse_type_pred(TypePred pred);
    std::unique_ptr<Expr> parse_subscript(std::unique_ptr<Expr> vec);
};

} // namespace mc
