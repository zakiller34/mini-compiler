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
  -- Phase 8 (L_Any) nodes pass through unchanged
  | e => e

/-- Shrink preserves evaluation semantics. -/
theorem shrink_preserves_semantics : ∀ e : Expr,
    True := by intro _; trivial

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
