#pragma once

#include "type.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class UnaryOp { Neg, Not };
enum class BinaryOp { Add, Sub, And, Or, Eq, Lt, Le, Gt, Ge };

enum class NodeKind {
  Int, Bool, Var, Read, Unary, Binary, If, Let,
  While, SetBang, Begin, Void, Get,
  Vector, VectorRef, VectorSet, VectorLength,
  Allocate, Collect, GlobalValue
};

/// Base class for all expressions in L_While.
class Expr {
public:
  virtual ~Expr() = default;

  virtual NodeKind kind() const = 0;

  /// @brief S-expression pretty-printer
  /// @ensures result is valid S-expr string
  virtual std::string dump() const = 0;
};

class IntExpr : public Expr {
public:
  int64_t value;
  explicit IntExpr(int64_t v) : value(v) {}
  NodeKind kind() const override { return NodeKind::Int; }
  std::string dump() const override;
};

class BoolExpr : public Expr {
public:
  bool value;
  explicit BoolExpr(bool v) : value(v) {}
  NodeKind kind() const override { return NodeKind::Bool; }
  std::string dump() const override;
};

class VarExpr : public Expr {
public:
  std::string name;
  explicit VarExpr(std::string n) : name(std::move(n)) {}
  NodeKind kind() const override { return NodeKind::Var; }
  std::string dump() const override;
};

class ReadExpr : public Expr {
public:
  NodeKind kind() const override { return NodeKind::Read; }
  std::string dump() const override;
};

class UnaryExpr : public Expr {
public:
  UnaryOp op;
  std::unique_ptr<Expr> operand;
  UnaryExpr(UnaryOp o, std::unique_ptr<Expr> e)
      : op(o), operand(std::move(e)) {}
  NodeKind kind() const override { return NodeKind::Unary; }
  std::string dump() const override;
};

class BinaryExpr : public Expr {
public:
  BinaryOp op;
  std::unique_ptr<Expr> lhs;
  std::unique_ptr<Expr> rhs;
  BinaryExpr(BinaryOp o, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r)
      : op(o), lhs(std::move(l)), rhs(std::move(r)) {}
  NodeKind kind() const override { return NodeKind::Binary; }
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
  NodeKind kind() const override { return NodeKind::If; }
  std::string dump() const override;
};

class LetExpr : public Expr {
public:
  std::string var;
  std::unique_ptr<Expr> init;
  std::unique_ptr<Expr> body;
  LetExpr(std::string v, std::unique_ptr<Expr> i, std::unique_ptr<Expr> b)
      : var(std::move(v)), init(std::move(i)), body(std::move(b)) {}
  NodeKind kind() const override { return NodeKind::Let; }
  std::string dump() const override;
};

class WhileExpr : public Expr {
public:
  std::unique_ptr<Expr> cond;
  std::unique_ptr<Expr> body;
  WhileExpr(std::unique_ptr<Expr> c, std::unique_ptr<Expr> b)
      : cond(std::move(c)), body(std::move(b)) {}
  NodeKind kind() const override { return NodeKind::While; }
  std::string dump() const override;
};

class SetBangExpr : public Expr {
public:
  std::string var_name;
  std::unique_ptr<Expr> expr;
  SetBangExpr(std::string v, std::unique_ptr<Expr> e)
      : var_name(std::move(v)), expr(std::move(e)) {}
  NodeKind kind() const override { return NodeKind::SetBang; }
  std::string dump() const override;
};

class BeginExpr : public Expr {
public:
  std::vector<std::unique_ptr<Expr>> exprs;
  explicit BeginExpr(std::vector<std::unique_ptr<Expr>> es)
      : exprs(std::move(es)) {}
  NodeKind kind() const override { return NodeKind::Begin; }
  std::string dump() const override;
};

class VoidExpr : public Expr {
public:
  NodeKind kind() const override { return NodeKind::Void; }
  std::string dump() const override;
};

/// @brief Introduced by uncover_get, never parsed
class GetExpr : public Expr {
public:
  std::string name;
  explicit GetExpr(std::string n) : name(std::move(n)) {}
  NodeKind kind() const override { return NodeKind::Get; }
  std::string dump() const override;
};

/// @brief Tuple constructor: vector(e1, e2, ...)
class VectorExpr : public Expr {
public:
  std::vector<std::unique_ptr<Expr>> elems;
  explicit VectorExpr(std::vector<std::unique_ptr<Expr>> es)
      : elems(std::move(es)) {}
  NodeKind kind() const override { return NodeKind::Vector; }
  std::string dump() const override;
};

/// @brief Tuple element access: vec[index]
class VectorRefExpr : public Expr {
public:
  std::unique_ptr<Expr> vec;
  int64_t index;
  VectorRefExpr(std::unique_ptr<Expr> v, int64_t i)
      : vec(std::move(v)), index(i) {}
  NodeKind kind() const override { return NodeKind::VectorRef; }
  std::string dump() const override;
};

/// @brief Tuple element mutation: vec[index] = val
class VectorSetExpr : public Expr {
public:
  std::unique_ptr<Expr> vec;
  int64_t index;
  std::unique_ptr<Expr> val;
  VectorSetExpr(std::unique_ptr<Expr> v, int64_t i, std::unique_ptr<Expr> va)
      : vec(std::move(v)), index(i), val(std::move(va)) {}
  NodeKind kind() const override { return NodeKind::VectorSet; }
  std::string dump() const override;
};

/// @brief Tuple length: length(vec)
class VectorLengthExpr : public Expr {
public:
  std::unique_ptr<Expr> vec;
  explicit VectorLengthExpr(std::unique_ptr<Expr> v) : vec(std::move(v)) {}
  NodeKind kind() const override { return NodeKind::VectorLength; }
  std::string dump() const override;
};

/// @brief Introduced by expose_allocation: allocate(len, type)
class AllocateExpr : public Expr {
public:
  int64_t len;
  TypePtr type;
  AllocateExpr(int64_t l, TypePtr t) : len(l), type(std::move(t)) {}
  NodeKind kind() const override { return NodeKind::Allocate; }
  std::string dump() const override;
};

/// @brief Introduced by expose_allocation: collect(bytes)
class CollectExpr : public Expr {
public:
  int64_t bytes;
  explicit CollectExpr(int64_t b) : bytes(b) {}
  NodeKind kind() const override { return NodeKind::Collect; }
  std::string dump() const override;
};

/// @brief Introduced by expose_allocation: global_value(name)
class GlobalValueExpr : public Expr {
public:
  std::string name;
  explicit GlobalValueExpr(std::string n) : name(std::move(n)) {}
  NodeKind kind() const override { return NodeKind::GlobalValue; }
  std::string dump() const override;
};

class Program {
public:
  std::unique_ptr<Expr> body;
  explicit Program(std::unique_ptr<Expr> b) : body(std::move(b)) {}
  std::string dump() const;
};
