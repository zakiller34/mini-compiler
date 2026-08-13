import MiniCompiler.AST
import MiniCompiler.Passes.Tagging

/-!
# Cast Insertion — L_Dyn → L_Any (Phase 8)

`cast_insert` makes every subexpression have type `Any`: literals are
injected, primitive operands are projected back to their flat type, and the
result is injected again (Siek 2023, figure 9.10).
Mirrors src/passes/cast_insert.cpp.
-/

namespace MiniCompiler

/-- The tagged constant `#f`, the only falsy value of L_Dyn. -/
def anyFalse : Expr := .inject (.bool false) .bool

/-- `(eq? e (inject #f Boolean))` — true exactly when `e` is falsy. -/
def isFalsy (e : Expr) : Expr := .binary .eq e anyFalse

/-- `(Any … -> Any)` with `n` parameters. -/
def anyFunTy (n : Nat) : Ty := .fun (List.replicate n Ty.any) Ty.any

/-- `(Vector Any … Any)` with `n` elements. -/
def anyVecTy (n : Nat) : Ty := .vector (List.replicate n Ty.any)

/-- Cast insertion. Only the representative cases of figure 9.10 are given;
    the remaining forms are structural. -/
def castInsert : Expr → Expr
  | .int n => .inject (.int n) .int
  | .bool b => .inject (.bool b) .bool
  | .read => .inject .read .int
  | .void_ => .inject .void_ .void
  | .var n => .var n
  | .get n => .get n
  | .binary .add l r =>
    .inject (.binary .add (.project (castInsert l) .int)
                           (.project (castInsert r) .int)) .int
  | .binary .sub l r =>
    .inject (.binary .sub (.project (castInsert l) .int)
                           (.project (castInsert r) .int)) .int
  | .binary .lt l r =>
    .inject (.binary .lt (.project (castInsert l) .int)
                          (.project (castInsert r) .int)) .bool
  | .binary .eq l r =>
    .inject (.binary .eq (castInsert l) (castInsert r)) .bool
  | .if_ c t e => .if_ (isFalsy (castInsert c)) (castInsert e) (castInsert t)
  | .let_ v i b => .let_ v (castInsert i) (castInsert b)
  | .apply f args =>
    .apply (.project (castInsert f) (anyFunTy args.length))
           (args.map castInsert)
  | .vector_ es => .inject (.vector_ (es.map castInsert)) (anyVecTy es.length)
  | e => e

/-- Cast insertion only ever mentions flat types in the casts it inserts:
    the function type it projects a callee to is `(Any … -> Any)`.

    Previously `sorry`. It is provable now only because `Ty`'s `BEq` was
    replaced by a structural definition that reduces — see `AST.lean`. -/
theorem cast_insert_uses_flat_types (n : Nat) : isFlat (anyFunTy n) = true := by
  simp [isFlat, anyFunTy]

/-- Likewise for the tuple type it injects a vector at. -/
theorem cast_insert_uses_flat_vec_types (n : Nat) :
    isFlat (anyVecTy n) = true := by
  simp [isFlat, anyVecTy]

-- NOTE: `cast_insert_types_any` and `cast_insert_preserves_semantics` are
-- deliberately absent. The typing invariant ("every result of `castInsert` has
-- type `Any`") is true and statable, but `castInsert` above models only the
-- representative forms of figure 9.10 — the remaining forms fall through
-- `| e => e`, which makes the invariant FALSE of this model. Stating it with a
-- `sorry` would assert something untrue. Completing the pass is the prerequisite.
-- Semantics preservation needs an evaluator; see the README scope table.

end MiniCompiler
