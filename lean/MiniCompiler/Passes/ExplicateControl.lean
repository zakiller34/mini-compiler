import MiniCompiler.AST

/-!
# Explicate Control — L_While

Convert AST to C_Var IR with explicit control flow (basic blocks).
-/

namespace MiniCompiler

-- C_Var IR types

inductive Atom where
  | int (value : Int) : Atom
  | bool (val : Bool) : Atom
  | var (name : String) : Atom
  deriving Repr, DecidableEq

inductive CCmpOp where
  | eq | lt | le | gt | ge
  deriving Repr, DecidableEq

inductive CExpr where
  | atom (a : Atom) : CExpr
  | read : CExpr
  | unary (op : UnaryOp) (a : Atom) : CExpr
  | binary (op : BinaryOp) (lhs rhs : Atom) : CExpr
  | cmp (op : CCmpOp) (lhs rhs : Atom) : CExpr
  | not_ (a : Atom) : CExpr
  deriving Repr

structure Assign where
  var : String
  expr : CExpr
  deriving Repr

inductive Tail where
  | return_ (e : CExpr) : Tail
  | goto_ (label : String) : Tail
  | ifStmt (op : CCmpOp) (lhs rhs : Atom)
           (then_ else_ : String) : Tail
  deriving Repr

structure BasicBlock where
  stmts : List Assign
  tail : Tail
  deriving Repr

structure CProgram where
  blocks : List (String × BasicBlock)
  deriving Repr

/-- Explicate control: AST → C_Var IR with basic blocks. -/
def explicate_control (p : Program) : CProgram := sorry

/-- Start block exists in output. -/
theorem explicate_has_start : ∀ p : Program,
    True := sorry

end MiniCompiler
