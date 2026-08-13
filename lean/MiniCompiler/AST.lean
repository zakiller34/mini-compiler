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
  deriving Repr

-- `deriving BEq` on `Ty` produces an instance compiled by well-founded
-- recursion (because of the nested `List Ty`), so `Ty.any == Ty.any` is not
-- provable by `rfl` or `decide` and every proof touching type equality gets
-- stuck. `deriving DecidableEq` does not apply to this nested inductive at all.
-- The structural definition below reduces, which is what makes those proofs go
-- through. Do NOT replace it with `native_decide`: that closes such goals but
-- adds the `ofReduceBool` axiom, which `Hygiene.lean` rejects.
mutual
/-- Structural equality on types. -/
def Ty.beq : Ty → Ty → Bool
  | .int, .int => true
  | .bool, .bool => true
  | .void, .void => true
  | .any, .any => true
  | .vector ts₁, .vector ts₂ => Ty.beqList ts₁ ts₂
  | .fun ps₁ r₁, .fun ps₂ r₂ => Ty.beqList ps₁ ps₂ && Ty.beq r₁ r₂
  | _, _ => false

/-- Pointwise structural equality on type lists. -/
def Ty.beqList : List Ty → List Ty → Bool
  | [], [] => true
  | t₁ :: ts₁, t₂ :: ts₂ => Ty.beq t₁ t₂ && Ty.beqList ts₁ ts₂
  | _, _ => false
end

instance : BEq Ty := ⟨Ty.beq⟩

mutual
/-- `Ty.beq` is reflexive. -/
theorem Ty.beq_refl : ∀ t : Ty, Ty.beq t t = true
  | .int | .bool | .void | .any => rfl
  | .vector ts => by simp [Ty.beq, Ty.beqList_refl ts]
  | .fun ps r => by simp [Ty.beq, Ty.beqList_refl ps, Ty.beq_refl r]

/-- `Ty.beqList` is reflexive. -/
theorem Ty.beqList_refl : ∀ ts : List Ty, Ty.beqList ts ts = true
  | [] => rfl
  | t :: ts => by simp [Ty.beqList, Ty.beq_refl t, Ty.beqList_refl ts]
end

@[simp] theorem Ty.beq_self (t : Ty) : (t == t) = true := Ty.beq_refl t

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

-- Shared syntactic query. Several passes are specified by "after me, no `var`
-- node names anything in this set" (reveal_functions: function names;
-- uncover_get: mutable variables), so the predicate lives here rather than
-- being duplicated per pass.
mutual
/-- No `var` node in `e` mentions a name from `ns`. -/
def noVarIn (ns : List String) : Expr → Bool
  | .var n => !ns.contains n
  | .unary _ e => noVarIn ns e
  | .binary _ l r => noVarIn ns l && noVarIn ns r
  | .if_ c t e => noVarIn ns c && noVarIn ns t && noVarIn ns e
  | .let_ _ i b => noVarIn ns i && noVarIn ns b
  | .while_ c b => noVarIn ns c && noVarIn ns b
  | .set_ _ e => noVarIn ns e
  | .begin es => noVarInL ns es
  | .vector_ es => noVarInL ns es
  | .vectorRef v _ => noVarIn ns v
  | .vectorSet v _ e => noVarIn ns v && noVarIn ns e
  | .vectorLength v => noVarIn ns v
  | .apply f args => noVarIn ns f && noVarInL ns args
  | .lambda _ _ b => noVarIn ns b
  | .procArity e => noVarIn ns e
  | .closure _ es => noVarInL ns es
  | .inject e _ => noVarIn ns e
  | .project e _ => noVarIn ns e
  | .typePred _ e => noVarIn ns e
  | .anyVectorRef v i => noVarIn ns v && noVarIn ns i
  | .anyVectorSet v i x => noVarIn ns v && noVarIn ns i && noVarIn ns x
  | .anyVectorLength v => noVarIn ns v
  | .makeAny e _ => noVarIn ns e
  | .tagOfAny e => noVarIn ns e
  | .valueOf e _ => noVarIn ns e
  | _ => true

/-- Pointwise lifting of `noVarIn` to a list of sub-expressions. -/
def noVarInL (ns : List String) : List Expr → Bool
  | [] => true
  | e :: es => noVarIn ns e && noVarInL ns es
end
