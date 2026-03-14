/-!
# MiniCompiler AST — L_Var

Mirror of C++ AST as Lean 4 inductive types.
-/

inductive UnaryOp where
  | neg : UnaryOp
  deriving Repr, DecidableEq

inductive BinaryOp where
  | add : BinaryOp
  | sub : BinaryOp
  deriving Repr, DecidableEq

inductive Expr where
  | int (value : Int) : Expr
  | var (name : String) : Expr
  | read : Expr
  | unary (op : UnaryOp) (operand : Expr) : Expr
  | binary (op : BinaryOp) (lhs rhs : Expr) : Expr
  | let_ (var : String) (init body : Expr) : Expr
  deriving Repr

structure Program where
  body : Expr
  deriving Repr
