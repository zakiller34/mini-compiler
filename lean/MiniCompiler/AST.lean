/-!
# MiniCompiler AST — L_Lambda

Mirror of C++ AST as Lean 4 inductive types.
Covers Phase 1-7: arithmetic, conditionals, loops, tuples, functions, closures.
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
  -- Phase 8 (L_Any): the dynamic type
  | any : Ty
  deriving Repr, BEq

/-- Runtime type predicates of L_Dyn: integer?, boolean?, ... -/
inductive TypePred where
  | integer : TypePred
  | boolean : TypePred
  | vector : TypePred
  | procedure : TypePred
  | void : TypePred
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
  | vector_ (elems : List Expr) : Expr
  | vectorRef (vec : Expr) (index : Nat) : Expr
  | vectorSet (vec : Expr) (index : Nat) (val : Expr) : Expr
  | vectorLength (vec : Expr) : Expr
  | apply (func : Expr) (args : List Expr) : Expr
  | funRef (name : String) (arity : Nat) : Expr
  -- Phase 7 (L_Lambda): anonymous functions & closures
  | lambda (params : List (String × Ty)) (retType : Ty) (body : Expr) : Expr
  | procArity (expr : Expr) : Expr
  | closure (arity : Nat) (elems : List Expr) : Expr
  -- Phase 8 (L_Any): tagged values (Siek 2023, figure 9.5)
  | inject (expr : Expr) (ftype : Ty) : Expr
  | project (expr : Expr) (ftype : Ty) : Expr
  | typePred (pred : TypePred) (expr : Expr) : Expr
  | anyVectorRef (vec idx : Expr) : Expr
  | anyVectorSet (vec idx val : Expr) : Expr
  | anyVectorLength (vec : Expr) : Expr
  -- Introduced by reveal_casts (Siek 2023, section 9.5)
  | makeAny (expr : Expr) (tag : Nat) : Expr
  | tagOfAny (expr : Expr) : Expr
  | valueOf (expr : Expr) (ftype : Ty) : Expr
  | exit_ : Expr
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
