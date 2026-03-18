/-!
# MiniCompiler AST — L_While

Mirror of C++ AST as Lean 4 inductive types.
-/

inductive UnaryOp where
  | neg : UnaryOp
  | not : UnaryOp
  deriving Repr, DecidableEq

inductive BinaryOp where
  | add : BinaryOp
  | sub : BinaryOp
  | and_ : BinaryOp
  | or_ : BinaryOp
  | eq : BinaryOp
  | lt : BinaryOp
  | le : BinaryOp
  | gt : BinaryOp
  | ge : BinaryOp
  deriving Repr, DecidableEq

/-- Type of an expression in L_While. -/
inductive Ty where
  | int : Ty
  | bool : Ty
  | void : Ty
  deriving Repr, DecidableEq

inductive Expr where
  | int (value : Int) : Expr
  | bool (val : Bool) : Expr
  | var (name : String) : Expr
  | read : Expr
  | unary (op : UnaryOp) (operand : Expr) : Expr
  | binary (op : BinaryOp) (lhs rhs : Expr) : Expr
  | if_ (cond then_ else_ : Expr) : Expr
  | let_ (var : String) (init body : Expr) : Expr
  | while_ (cond body : Expr) : Expr
  | set_ (var : String) (expr : Expr) : Expr
  | begin (exprs : List Expr) : Expr
  | void_ : Expr
  | get (name : String) : Expr
  deriving Repr

structure Program where
  body : Expr
  deriving Repr
