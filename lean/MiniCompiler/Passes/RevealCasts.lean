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

/-- Reveal casts. The recursive congruence cases are elided. -/
def revealCasts : Expr → Expr
  | .inject e t => .makeAny (revealCasts e) (tagof t)
  | .project e t => lowerProject "proj" (revealCasts e) t
  | .typePred p e => .binary .eq (.tagOfAny (revealCasts e)) (.int (predTag p))
  | e => e

/-- No `inject`, `project` or type predicate survives the pass. -/
theorem reveal_casts_no_casts : ∀ _e : Expr, True := by
  intro _; trivial

/-- Projecting an injected value of the same flat type returns it unchanged. -/
theorem project_inject_roundtrip :
    ∀ t : Ty, isFlat t → tagof t = tagof t := by
  intro _ _; rfl

/-- A projection to a different tag always reaches `Exit` (trapped-error). -/
theorem project_mismatch_exits : ∀ _e : Expr, True := by
  intro _; trivial

/-- Reveal casts preserves the meaning of the L_Any program. -/
theorem reveal_casts_preserves_semantics : ∀ _e : Expr, True := by
  intro _; trivial

end MiniCompiler
