import MiniCompiler.AST

/-!
# Liveness Analysis

Backward dataflow: compute live-after sets per instruction.
Uses List String as set representation (no Mathlib dependency).
-/

/-- An instruction's read/write sets -/
structure InstrRW where
  reads : List String
  writes : List String

/-- Live-before = (live-after \ writes) ∪ reads -/
def live_before (rw : InstrRW) (live_after : List String) : List String :=
  (live_after.filter (· ∉ rw.writes)) ++ rw.reads

/-- Backward pass: fold from right, accumulating live-after sets.
    Returns live-after for each instruction. -/
def analyze_liveness_go : List InstrRW → List String → List (List String)
  | [], _ => []
  | rw :: rest, succ_live =>
    let la := succ_live
    let lb := live_before rw la
    la :: analyze_liveness_go rest lb

/-- Liveness analysis: reverse instrs, fold, reverse result -/
def analyze_liveness (instrs : List InstrRW) : List (List String) :=
  (analyze_liveness_go instrs.reverse []).reverse

/-- Soundness: reads are always included in live_before -/
theorem live_before_contains_reads (rw : InstrRW) (la : List String)
    (v : String) (hv : v ∈ rw.reads) :
    v ∈ live_before rw la := by
  simp [live_before]
  exact Or.inr hv

/-- Soundness: non-written live vars are preserved -/
theorem live_before_preserves (rw : InstrRW) (la : List String)
    (v : String) (hla : v ∈ la) (hw : v ∉ rw.writes) :
    v ∈ live_before rw la := by
  simp [live_before]
  exact Or.inl ⟨hla, hw⟩

/-- Liveness is sound: every read variable appears in live_before -/
theorem liveness_sound (rw : InstrRW) (la : List String) (v : String)
    (hread : v ∈ rw.reads) : v ∈ live_before rw la :=
  live_before_contains_reads rw la v hread
