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
    Any_kw,
    Inject,
    Project,
    IntegerP,
    BooleanP,
    VectorP,
    ProcedureP,
    VoidP,
    Eof,
    Error
};

/// @brief A position in the source file. line == 0 means "unknown", which is
///        what nodes synthesised by a compiler pass carry.
struct SourceLoc {
    int line = 0;
    int col = 0;

    bool known() const { return line > 0; }
};

struct Token {
    TokenKind kind;
    std::string text;
    int64_t int_val = 0;
    SourceLoc loc;
};

/// @brief Hand-written lexer for L_Var
class Lexer {
  public:
    /// @brief Construct lexer from input stream
    /// @requires in is valid readable stream
    explicit Lexer(std::istream &in);

    /// @brief Get next token from input
    /// @ensures result.kind is valid TokenKind
    /// @ensures result.loc is the position of the token's first character
    Token next();

    /// @brief Position of the first character of the token last returned
    SourceLoc token_loc() const { return tok_start_; }

  private:
    std::istream &input_;
    int cur_ = ' ';
    int line_ = 1;
    int col_ = 0;
    /// Position of the character currently in cur_, i.e. the start of the
    /// token being scanned once skip_ws() has run.
    SourceLoc tok_start_;

    /// @brief Advance to next char from input
    /// @modifies line_, col_ — a newline advances the line and resets the column
    void advance();

    /// @brief Skip whitespace and comments
    void skip_ws();

    /// @brief Scan one token, recording its start in tok_start_
    /// @modifies tok_start_
    Token next_impl();
};

} // namespace mc
