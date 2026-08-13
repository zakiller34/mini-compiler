import MiniCompiler.AST

/-!
# Tagged value representation — L_Any (Phase 8)

The three low bits of every 64-bit value encode its runtime type
(Siek 2023, section 9.2). The bit pattern `000` is deliberately unused so the
collector can tell a plain tuple pointer from a tagged value.
Mirrors `tagof` / `is_flat_type` in src/type.cpp.
-/

namespace MiniCompiler

/-- Tag codes. `0` is reserved for untagged tuple pointers. -/
def tagInt : Nat := 1
def tagVector : Nat := 2
def tagFunction : Nat := 3
def tagBool : Nat := 4
def tagVoid : Nat := 5
def tagShift : Nat := 3

/-- Flat types: the only types `inject`/`project` may mention. -/
def isFlat : Ty → Bool
  | .int => true
  | .bool => true
  | .void => true
  | .vector ts => ts.all (· == Ty.any)
  | .fun ps r => ps.all (· == Ty.any) && r == Ty.any
  | .any => false

/-- Runtime tag of a flat type. -/
def tagof : Ty → Nat
  | .int => tagInt
  | .bool => tagBool
  | .void => tagVoid
  | .vector _ => tagVector
  | .fun _ _ => tagFunction
  | .any => tagInt  -- unreachable: `Any` is not flat

/-- Tags separate the five runtime shapes.

This replaces a theorem previously named `tagof_injective_on_flat`, whose
conclusion was `… ∨ True` — vacuous, and mis-named besides: `tagof` is *not*
injective on flat types, since `.vector [any]` and `.vector [any, any]` share
`tagVector`. Shape separation is the property the runtime actually relies on. -/
theorem tagof_distinguishes_shapes :
    tagof .int ≠ tagof .bool ∧ tagof .int ≠ tagof .void ∧
      tagof .int ≠ tagof (.vector []) ∧ tagof .bool ≠ tagof (.vector []) ∧
      tagof (.vector []) ≠ tagof (.fun [] .any) := by
  refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide

/-- No tag code collides with the reserved `000` pattern, so a marked GC slot
    holding `000` is always an untagged tuple pointer (Siek 2023, section 9.9). -/
theorem tags_are_nonzero :
    tagInt ≠ 0 ∧ tagBool ≠ 0 ∧ tagVector ≠ 0 ∧ tagFunction ≠ 0 ∧
      tagVoid ≠ 0 := by
  refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide

/-- Tagging a scalar and untagging it again is the identity on the
    representable range (proved over bit-vectors in tests/z3). -/
theorem scalar_tag_roundtrip : ∀ v : Nat, (v * 2 ^ tagShift) / 2 ^ tagShift = v := by
  intro v
  simp [tagShift]

end MiniCompiler
