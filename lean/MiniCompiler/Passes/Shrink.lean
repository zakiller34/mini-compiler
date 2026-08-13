import MiniCompiler.AST

/-!
# Shrink Pass — L_While

Desugar `and`/`or` to `if` expressions.
- `and(a, b)` → `if(a, b, false)`
- `or(a, b)` → `if(a, true, b)`
Pass through Phase 4 nodes (while, set!, begin, void).
-/

namespace MiniCompiler

/-- Desugar and/or to if expressions. -/
def shrink : Expr → Expr
  | .int v => .int v
  | .bool b => .bool b
  | .var n => .var n
  | .read => .read
  | .unary op e => .unary op (shrink e)
  | .binary .and_ lhs rhs => .if_ (shrink lhs) (shrink rhs) (.bool false)
  | .binary .or_ lhs rhs => .if_ (shrink lhs) (.bool true) (shrink rhs)
  | .binary op lhs rhs => .binary op (shrink lhs) (shrink rhs)
  | .if_ c t e => .if_ (shrink c) (shrink t) (shrink e)
  | .let_ v i b => .let_ v (shrink i) (shrink b)
  | .while_ c b => .while_ (shrink c) (shrink b)
  | .set_ v e => .set_ v (shrink e)
  | .begin es => .begin (es.map shrink)
  | .void_ => .void_
  | .get n => .get n
  | .vector_ es => .vector_ (es.map shrink)
  | .vectorRef v i => .vectorRef (shrink v) i
  | .vectorSet v i e => .vectorSet (shrink v) i (shrink e)
  | .vectorLength v => .vectorLength (shrink v)
  | .apply f args => .apply (shrink f) (args.map shrink)
  | .funRef n a => .funRef n a
  | .lambda ps rt b => .lambda ps rt (shrink b)
  | .procArity e => .procArity (shrink e)
  | .closure a es => .closure a (es.map shrink)
  -- Phase 8 (L_Any). These carry sub-expressions, so a catch-all `| e => e`
  -- silently left `and`/`or` nodes underneath them un-desugared.
  | .inject e t => .inject (shrink e) t
  | .project e t => .project (shrink e) t
  | .typePred p e => .typePred p (shrink e)
  | .anyVectorRef v i => .anyVectorRef (shrink v) (shrink i)
  | .anyVectorSet v i x => .anyVectorSet (shrink v) (shrink i) (shrink x)
  | .anyVectorLength v => .anyVectorLength (shrink v)
  | .makeAny e tag => .makeAny (shrink e) tag
  | .tagOfAny e => .tagOfAny (shrink e)
  | .valueOf e t => .valueOf (shrink e) t
  | .exit_ => .exit_

-- `Expr` is a *nested* inductive (`List Expr` in `begin`/`vector_`/`apply`/
-- `closure`), so the obvious `es.all noAndOr` formulation fails to elaborate:
-- Lean cannot infer structural recursion through `List.all`. The explicit
-- `mutual` companion over `List Expr` is what makes both the definition and the
-- induction below go through.
mutual
/-- No `and`/`or` node occurs anywhere in `e`. -/
def noAndOr : Expr → Bool
  | .binary .and_ _ _ => false
  | .binary .or_ _ _ => false
  | .binary _ l r => noAndOr l && noAndOr r
  | .unary _ e => noAndOr e
  | .if_ c t e => noAndOr c && noAndOr t && noAndOr e
  | .let_ _ i b => noAndOr i && noAndOr b
  | .while_ c b => noAndOr c && noAndOr b
  | .set_ _ e => noAndOr e
  | .begin es => noAndOrL es
  | .vector_ es => noAndOrL es
  | .vectorRef v _ => noAndOr v
  | .vectorSet v _ e => noAndOr v && noAndOr e
  | .vectorLength v => noAndOr v
  | .apply f args => noAndOr f && noAndOrL args
  | .lambda _ _ b => noAndOr b
  | .procArity e => noAndOr e
  | .closure _ es => noAndOrL es
  | .inject e _ => noAndOr e
  | .project e _ => noAndOr e
  | .typePred _ e => noAndOr e
  | .anyVectorRef v i => noAndOr v && noAndOr i
  | .anyVectorSet v i x => noAndOr v && noAndOr i && noAndOr x
  | .anyVectorLength v => noAndOr v
  | .makeAny e _ => noAndOr e
  | .tagOfAny e => noAndOr e
  | .valueOf e _ => noAndOr e
  | _ => true

/-- Pointwise lifting of `noAndOr` to a list of sub-expressions. -/
def noAndOrL : List Expr → Bool
  | [] => true
  | e :: es => noAndOr e && noAndOrL es
end

/-- Shrink eliminates every `and`/`or` node.

This is the syntactic half of what `shrink` is supposed to guarantee. The
semantic half (that the desugaring preserves evaluation) is not stated here: it
requires an operational semantics for L_Any, which this development does not
have. See the README's scope table. -/
theorem shrink_no_and_or (e : Expr) : noAndOr (shrink e) = true := by
  induction e using Expr.rec
    (motive_2 := fun es => noAndOrL (es.map shrink) = true) with
  | binary op l r ihl ihr => cases op <;> simp [shrink, noAndOr, ihl, ihr]
  | nil => simp [noAndOrL]
  | cons e es ih ihs => simp [noAndOrL, ih, ihs]
  | _ => simp_all [shrink, noAndOr]

/-- Bool encoding roundtrip: and/or desugaring is equivalent. -/
theorem bool_encoding_roundtrip : ∀ (a b : Bool),
    (a && b) = (if a then b else false) := by
  intro a b
  cases a <;> simp

/-- Or encoding roundtrip. -/
theorem or_encoding_roundtrip : ∀ (a b : Bool),
    (a || b) = (if a then true else b) := by
  intro a b
  cases a <;> simp

end MiniCompiler
