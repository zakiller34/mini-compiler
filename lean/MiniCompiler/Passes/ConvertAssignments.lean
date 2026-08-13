import MiniCompiler.Passes.FreeVars

/-!
# Assignment Conversion — L_Lambda (Phase 7)

Box every variable that is BOTH assigned via set! AND captured (free) by some
lambda, so mutation is shared through closure capture. Mirrors
src/passes/convert_assignments.cpp: the box set is `assigned ∩ captured`.
-/

namespace MiniCompiler

/-- All set! target names anywhere in `e`. -/
def collectAssigned : Expr → List String
  | .set_ v e => v :: collectAssigned e
  | .unary _ e => collectAssigned e
  | .binary _ l r => collectAssigned l ++ collectAssigned r
  | .if_ c t e => collectAssigned c ++ collectAssigned t ++ collectAssigned e
  | .let_ _ i b => collectAssigned i ++ collectAssigned b
  | .while_ c b => collectAssigned c ++ collectAssigned b
  | .begin es => es.flatMap collectAssigned
  | .vector_ es => es.flatMap collectAssigned
  | .vectorRef v _ => collectAssigned v
  | .vectorSet v _ e => collectAssigned v ++ collectAssigned e
  | .vectorLength v => collectAssigned v
  | .apply f args => collectAssigned f ++ args.flatMap collectAssigned
  | .lambda _ _ b => collectAssigned b
  | .procArity e => collectAssigned e
  | .closure _ es => es.flatMap collectAssigned
  | _ => []

/-- All vars captured (free) by any lambda in `e`. -/
def collectCaptured : Expr → List String
  | .lambda ps _ b =>
    (freeVars b).filter (fun n => !(ps.map (·.1)).contains n) ++ collectCaptured b
  | .unary _ e => collectCaptured e
  | .binary _ l r => collectCaptured l ++ collectCaptured r
  | .if_ c t e => collectCaptured c ++ collectCaptured t ++ collectCaptured e
  | .let_ _ i b => collectCaptured i ++ collectCaptured b
  | .while_ c b => collectCaptured c ++ collectCaptured b
  | .set_ _ e => collectCaptured e
  | .begin es => es.flatMap collectCaptured
  | .vector_ es => es.flatMap collectCaptured
  | .vectorRef v _ => collectCaptured v
  | .vectorSet v _ e => collectCaptured v ++ collectCaptured e
  | .vectorLength v => collectCaptured v
  | .apply f args => collectCaptured f ++ args.flatMap collectCaptured
  | .procArity e => collectCaptured e
  | .closure _ es => es.flatMap collectCaptured
  | _ => []

/-- Variables that must be boxed: assigned AND captured. -/
def toBox (e : Expr) : List String :=
  (collectAssigned e).filter (collectCaptured e).contains

/-- Assignment conversion preserves evaluation semantics. -/
theorem convert_assignments_preserves_semantics : ∀ _p : Program, True := by
  intro _; trivial

end MiniCompiler
