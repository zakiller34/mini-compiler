#pragma once

#include "../type.h"

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace mc::cir {

// -- Atoms --

struct IntAtom { int64_t value; };
struct BoolAtom { bool value; };
struct VarAtom { std::string name; };

using Atom = std::variant<IntAtom, BoolAtom, VarAtom>;

// -- C_Var expressions --

enum class CUnaryOp { Neg, Not };
enum class CBinaryOp { Add, Sub };
enum class CCmpOp { Eq, Lt, Le, Gt, Ge };

struct AtomExpr { Atom atom; };
struct CReadExpr {};
struct CUnaryExpr { CUnaryOp op; Atom operand; };
struct CBinaryExpr { CBinaryOp op; Atom lhs; Atom rhs; };
struct CCmpExpr { CCmpOp op; Atom lhs; Atom rhs; };
struct CNotExpr { Atom operand; };
struct CAllocateExpr { int64_t len; TypePtr type; };
struct CVectorRefExpr { Atom vec; int64_t index; };
struct CVectorSetExpr { Atom vec; int64_t index; Atom val; };
struct CVectorLengthExpr { Atom vec; };
struct CGlobalValueExpr { std::string name; };
struct CCollectExpr { int64_t bytes; };

using CExpr = std::variant<AtomExpr, CReadExpr, CUnaryExpr, CBinaryExpr,
                           CCmpExpr, CNotExpr,
                           CAllocateExpr, CVectorRefExpr, CVectorSetExpr,
                           CVectorLengthExpr, CGlobalValueExpr,
                           CCollectExpr>;

// -- Statements --

struct Assign { std::string var; CExpr expr; };

// -- Tails (block terminators) --

struct Return { CExpr expr; };
struct Goto { std::string label; };
struct IfStmt {
    CCmpOp op;
    Atom lhs;
    Atom rhs;
    std::string then_label;
    std::string else_label;
};

using Tail = std::variant<Return, Goto, IfStmt>;

// -- Basic block --

struct BasicBlock {
  std::vector<Assign> stmts;
  Tail tail;
};

// -- C_Var program --

struct CProgram {
  std::map<std::string, BasicBlock> blocks;
  std::map<std::string, TypePtr> var_types;
  std::string dump() const;
};

std::string dump_atom(const Atom &a);
std::string dump_cexpr(const CExpr &e);
std::string dump_tail(const Tail &t);

} // namespace mc::cir
