#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class UnaryOp { Neg, Not };
enum class BinaryOp { Add, Sub, And, Or, Eq, Lt, Le, Gt, Ge };

/// Base class for all expressions in L_While.
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

class BoolExpr : public Expr {
public:
  bool value;
  explicit BoolExpr(bool v) : value(v) {}
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

class IfExpr : public Expr {
public:
  std::unique_ptr<Expr> cond;
  std::unique_ptr<Expr> then_branch;
  std::unique_ptr<Expr> else_branch;
  IfExpr(std::unique_ptr<Expr> c, std::unique_ptr<Expr> t,
         std::unique_ptr<Expr> e)
      : cond(std::move(c)), then_branch(std::move(t)),
        else_branch(std::move(e)) {}
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

class WhileExpr : public Expr {
public:
  std::unique_ptr<Expr> cond;
  std::unique_ptr<Expr> body;
  WhileExpr(std::unique_ptr<Expr> c, std::unique_ptr<Expr> b)
      : cond(std::move(c)), body(std::move(b)) {}
  std::string dump() const override;
};

class SetBangExpr : public Expr {
public:
  std::string var_name;
  std::unique_ptr<Expr> expr;
  SetBangExpr(std::string v, std::unique_ptr<Expr> e)
      : var_name(std::move(v)), expr(std::move(e)) {}
  std::string dump() const override;
};

class BeginExpr : public Expr {
public:
  std::vector<std::unique_ptr<Expr>> exprs;
  explicit BeginExpr(std::vector<std::unique_ptr<Expr>> es)
      : exprs(std::move(es)) {}
  std::string dump() const override;
};

class VoidExpr : public Expr {
public:
  std::string dump() const override;
};

/// @brief Introduced by uncover_get, never parsed
class GetExpr : public Expr {
public:
  std::string name;
  explicit GetExpr(std::string n) : name(std::move(n)) {}
  std::string dump() const override;
};

class Program {
public:
  std::unique_ptr<Expr> body;
  explicit Program(std::unique_ptr<Expr> b) : body(std::move(b)) {}
  std::string dump() const;
};
