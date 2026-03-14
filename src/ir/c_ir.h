#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace cir {

// -- Atoms: values that need no further computation --

struct IntAtom {
  int64_t value;
};

struct VarAtom {
  std::string name;
};

using Atom = std::variant<IntAtom, VarAtom>;

// -- C_Var expressions --

enum class CUnaryOp { Neg };
enum class CBinaryOp { Add, Sub };

struct AtomExpr {
  Atom atom;
};

struct CReadExpr {};

struct CUnaryExpr {
  CUnaryOp op;
  Atom operand;
};

struct CBinaryExpr {
  CBinaryOp op;
  Atom lhs;
  Atom rhs;
};

using CExpr = std::variant<AtomExpr, CReadExpr, CUnaryExpr, CBinaryExpr>;

// -- Statements --

struct Assign {
  std::string var;
  CExpr expr;
};

// -- Basic block: sequence of assignments ending in a return --

struct BasicBlock {
  std::vector<Assign> stmts;
  CExpr ret;
};

// -- C_Var program: map of labels to basic blocks --

struct CProgram {
  std::map<std::string, BasicBlock> blocks;
  std::string dump() const;
};

std::string dump_atom(const Atom &a);
std::string dump_cexpr(const CExpr &e);

} // namespace cir
