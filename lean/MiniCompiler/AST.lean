/-!
# MiniCompiler AST — L_Fun

Mirror of C++ AST as Lean 4 inductive types.
Covers Phase 1-6: arithmetic, conditionals, loops, tuples, functions.
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

/-- Type of an expression in L_Fun. -/
inductive Ty where
  | int : Ty
  | bool : Ty
  | void : Ty
  | vector : List Ty → Ty
  | fun : List Ty → Ty → Ty
  deriving Repr, BEq

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
  | vector_ (elems : List Expr) : Expr
  | vectorRef (vec : Expr) (index : Nat) : Expr
  | vectorSet (vec : Expr) (index : Nat) (val : Expr) : Expr
  | vectorLength (vec : Expr) : Expr
  | apply (func : Expr) (args : List Expr) : Expr
  | funRef (name : String) (arity : Nat) : Expr
  deriving Repr

/-- Top-level function definition. -/
structure DefNode where
  name : String
  params : List (String × Ty)
  retType : Ty
  body : Expr
  deriving Repr

structure Program where
  defs : List DefNode := []
  body : Expr
  deriving Repr
