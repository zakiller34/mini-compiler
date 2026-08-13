import MiniCompiler.AST
import MiniCompiler.Passes.Tagging

/-!
# Reveal Casts — L_Any → L_Any (Phase 8)

`reveal_casts` lowers `inject`/`project`/type predicates to explicit tag
manipulation: `make-any`, `tag-of-any`, `value-of` and `Exit`
(Siek 2023, section 9.5). Mirrors src/passes/reveal_casts.cpp.
-/

namespace MiniCompiler

/-- Tag code tested by a runtime type predicate. -/
def predTag : TypePred → Nat
  | .integer => tagInt
  | .boolean => tagBool
  | .vector => tagVector
  | .procedure => tagFunction
  | .void => tagVoid

/-- `(Project e T)` becomes a tag test guarding a `value-of`, else `Exit`. -/
def lowerProject (tmp : String) (src : Expr) (t : Ty) : Expr :=
  .let_ tmp src
    (.if_ (.binary .eq (.tagOfAny (.var tmp)) (.int (tagof t)))
          (.valueOf (.var tmp) t)
          .exit_)

/-- Reveal casts.

Known divergence from `src/passes/reveal_casts.cpp`: the C++ pass generates a
fresh `proj.N` binder per projection and additionally emits a shape check (tuple
length / procedure arity) that this model omits. The fixed `"proj"` binder here
is sound only because the temporary is consumed immediately by the surrounding
`if`; it is not a faithful model of the freshness discipline. -/
def revealCasts : Expr → Expr
  | .inject e t => .makeAny (revealCasts e) (tagof t)
  | .project e t => lowerProject "proj" (revealCasts e) t
  | .typePred p e => .binary .eq (.tagOfAny (revealCasts e)) (.int (predTag p))
  -- Congruence cases. These were previously elided behind `| e => e`, which
  -- made the pass a no-op below the first non-cast node.
  | .unary op e => .unary op (revealCasts e)
  | .binary op l r => .binary op (revealCasts l) (revealCasts r)
  | .if_ c t e => .if_ (revealCasts c) (revealCasts t) (revealCasts e)
  | .let_ v i b => .let_ v (revealCasts i) (revealCasts b)
  | .while_ c b => .while_ (revealCasts c) (revealCasts b)
  | .set_ v e => .set_ v (revealCasts e)
  | .begin es => .begin (es.map revealCasts)
  | .vector_ es => .vector_ (es.map revealCasts)
  | .vectorRef v i => .vectorRef (revealCasts v) i
  | .vectorSet v i e => .vectorSet (revealCasts v) i (revealCasts e)
  | .vectorLength v => .vectorLength (revealCasts v)
  | .apply f args => .apply (revealCasts f) (args.map revealCasts)
  | .lambda ps rt b => .lambda ps rt (revealCasts b)
  | .procArity e => .procArity (revealCasts e)
  | .closure a es => .closure a (es.map revealCasts)
  | .anyVectorRef v i => .anyVectorRef (revealCasts v) (revealCasts i)
  | .anyVectorSet v i x =>
    .anyVectorSet (revealCasts v) (revealCasts i) (revealCasts x)
  | .anyVectorLength v => .anyVectorLength (revealCasts v)
  | .makeAny e tag => .makeAny (revealCasts e) tag
  | .tagOfAny e => .tagOfAny (revealCasts e)
  | .valueOf e t => .valueOf (revealCasts e) t
  | e => e

mutual
/-- No `inject`, `project` or type-predicate node occurs anywhere in `e`. -/
def noCasts : Expr → Bool
  | .inject _ _ => false
  | .project _ _ => false
  | .typePred _ _ => false
  | .unary _ e => noCasts e
  | .binary _ l r => noCasts l && noCasts r
  | .if_ c t e => noCasts c && noCasts t && noCasts e
  | .let_ _ i b => noCasts i && noCasts b
  | .while_ c b => noCasts c && noCasts b
  | .set_ _ e => noCasts e
  | .begin es => noCastsL es
  | .vector_ es => noCastsL es
  | .vectorRef v _ => noCasts v
  | .vectorSet v _ e => noCasts v && noCasts e
  | .vectorLength v => noCasts v
  | .apply f args => noCasts f && noCastsL args
  | .lambda _ _ b => noCasts b
  | .procArity e => noCasts e
  | .closure _ es => noCastsL es
  | .anyVectorRef v i => noCasts v && noCasts i
  | .anyVectorSet v i x => noCasts v && noCasts i && noCasts x
  | .anyVectorLength v => noCasts v
  | .makeAny e _ => noCasts e
  | .tagOfAny e => noCasts e
  | .valueOf e _ => noCasts e
  | _ => true

/-- Pointwise lifting of `noCasts` to a list of sub-expressions. -/
def noCastsL : List Expr → Bool
  | [] => true
  | e :: es => noCasts e && noCastsL es
end

/-- No `inject`, `project` or type predicate survives the pass. -/
theorem reveal_casts_no_casts (e : Expr) : noCasts (revealCasts e) = true := by
  induction e using Expr.rec
    (motive_2 := fun es => noCastsL (es.map revealCasts) = true) with
  | nil => simp [noCastsL]
  | cons e es ih ihs => simp [noCastsL, ih, ihs]
  | _ => simp_all [revealCasts, noCasts, lowerProject]

/-- The lowering of `project` always puts `Exit` on the tag-mismatch branch —
    a failed projection is a trapped error, never a silent fallthrough.

    This replaces a vacuous `project_mismatch_exits`. The full statement — that
    evaluation actually *reaches* that `Exit` when the tags differ — needs an
    operational semantics; this is the syntactic half. -/
theorem lowerProject_else_is_exit (tmp : String) (src : Expr) (t : Ty) :
    ∃ c v, lowerProject tmp src t = .let_ tmp src (.if_ c v .exit_) :=
  ⟨_, _, rfl⟩

-- NOTE: `project_inject_roundtrip` and `reveal_casts_preserves_semantics` are
-- deliberately absent. The former was stated as `tagof t = tagof t`, i.e.
-- reflexivity wearing the name of a roundtrip property; the real statement
-- ("projecting an injected value returns it") is about evaluation and needs a
-- semantics. The Z3 suite covers the bit-level roundtrip over machine integers
-- in `tests/z3/test_tagging_z3.cpp`.

end MiniCompiler
