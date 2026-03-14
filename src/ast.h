#pragma once

#include <cstdint>
#include <memory>
#include <string>

enum class UnaryOp { Neg };
enum class BinaryOp { Add, Sub };

/// Base class for all expressions in L_Var.
class Expr {
public:
  virtual ~Expr() = default;

  /// @brief S-expression pretty-printer
  /// @ensures result is valid S-expr string
  virtual std::string dump() const = 0;
};

class IntExpr : public Expr {
public:
  int64_t value;
  explicit IntExpr(int64_t v) : value(v) {}
  std::string dump() const override;
};

class VarExpr : public Expr {
public:
  std::string name;
  explicit VarExpr(std::string n) : name(std::move(n)) {}
  std::string dump() const override;
};

class ReadExpr : public Expr {
public:
  std::string dump() const override;
};

class UnaryExpr : public Expr {
public:
  UnaryOp op;
  std::unique_ptr<Expr> operand;
  UnaryExpr(UnaryOp o, std::unique_ptr<Expr> e)
      : op(o), operand(std::move(e)) {}
  std::string dump() const override;
};

class BinaryExpr : public Expr {
public:
  BinaryOp op;
  std::unique_ptr<Expr> lhs;
  std::unique_ptr<Expr> rhs;
  BinaryExpr(BinaryOp o, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r)
      : op(o), lhs(std::move(l)), rhs(std::move(r)) {}
  std::string dump() const override;
};

class LetExpr : public Expr {
public:
  std::string var;
  std::unique_ptr<Expr> init;
  std::unique_ptr<Expr> body;
  LetExpr(std::string v, std::unique_ptr<Expr> i, std::unique_ptr<Expr> b)
      : var(std::move(v)), init(std::move(i)), body(std::move(b)) {}
  std::string dump() const override;
};

class Program {
public:
  std::unique_ptr<Expr> body;
  explicit Program(std::unique_ptr<Expr> b) : body(std::move(b)) {}
  std::string dump() const;
};
