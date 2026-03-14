/-!
# Interference Graph

Undirected graph: edge between vars simultaneously live.
-/

/-- A Location is either a variable name or a register index -/
inductive Location where
  | var : String → Location
  | reg : Nat → Location
  deriving DecidableEq, Repr

/-- Interference graph as symmetric adjacency relation -/
structure IGraph where
  adj : Location → Location → Prop
  symm : ∀ u v, adj u v → adj v u
  irrefl : ∀ u, ¬adj u u

/-- Two variables interfere if both in same live set and distinct -/
def vars_interfere (live_set : List String) (u v : String) : Prop :=
  u ∈ live_set ∧ v ∈ live_set ∧ u ≠ v

/-- Interference is symmetric -/
theorem vars_interfere_symm (live : List String) (u v : String)
    (h : vars_interfere live u v) : vars_interfere live v u := by
  obtain ⟨hu, hv, hne⟩ := h
  exact ⟨hv, hu, hne.symm⟩

/-- Interference is irreflexive -/
theorem vars_interfere_irrefl (live : List String) (u : String) :
    ¬vars_interfere live u u := by
  intro ⟨_, _, hne⟩
  exact hne rfl

/-- Interference correctness: edge iff simultaneously live -/
theorem interference_correct (live : List String) (u v : String)
    (hne : u ≠ v) :
    vars_interfere live u v ↔ (u ∈ live ∧ v ∈ live) := by
  constructor
  · intro ⟨hu, hv, _⟩; exact ⟨hu, hv⟩
  · intro ⟨hu, hv⟩; exact ⟨hu, hv, hne⟩
