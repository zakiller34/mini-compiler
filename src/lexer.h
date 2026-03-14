#pragma once

#include <cstdint>
#include <istream>
#include <string>

enum class TokenKind {
    IntLit,
    Ident,
    Let,
    Read,
    In,
    Plus,
    Minus,
    LParen,
    RParen,
    Equals,
    Semicolon,
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
