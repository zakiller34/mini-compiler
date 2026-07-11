#pragma once

#include <cstdint>
#include <istream>
#include <string>

namespace mc {

enum class TokenKind {
    IntLit,
    Ident,
    Let,
    Read,
    In,
    True,
    False,
    If,
    Else,
    And,
    Or,
    Not,
    Plus,
    Minus,
    LParen,
    RParen,
    LBrace,
    RBrace,
    Equals,
    EqEq,
    Lt,
    Le,
    Gt,
    Ge,
    Semicolon,
    While,
    Void,
    Begin,
    Set,
    Bang,
    Comma,
    LBracket,
    RBracket,
    VectorKw,
    Length,
    Fn,
    Arrow,
    Colon,
    Int_kw,
    Bool_kw,
    Lambda,
    ProcArity,
    Eof,
    Error
};

struct Token {
    TokenKind kind;
    std::string text;
    int64_t int_val = 0;
};

/// @brief Hand-written lexer for L_Var
class Lexer {
  public:
    /// @brief Construct lexer from input stream
    /// @requires in is valid readable stream
    explicit Lexer(std::istream &in);

    /// @brief Get next token from input
    /// @ensures result.kind is valid TokenKind
    Token next();

  private:
    std::istream &input_;
    int cur_ = ' ';

    /// @brief Advance to next char from input
    void advance();

    /// @brief Skip whitespace and comments
    void skip_ws();
};

} // namespace mc
