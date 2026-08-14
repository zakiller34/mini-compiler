---
"mini-compiler": patch
---

README made self-contained and illustrated; ROADMAP corrected where it still
claimed what the proof-hygiene audit retracted.

The README no longer sends the reader anywhere else. All 24 markdown links into
the repo became plain inline paths — kept, and normalised to full repo-root paths,
because they are the evidence that makes each claim checkable, but no longer
clickable links that break when the article is ported off GitHub. Everything it
used to defer is now inlined: the per-phase plan (Phases 0-8, with book chapters),
the real reason Phase 9 was declined (higher-order casts need proxy objects, which
is where the verification story would have to be abandoned rather than merely
bounded), the clang-format drift, and the git evidence for the headline claim.
Two known gaps recovered from ROADMAP and added: source positions do not survive
the pipeline, and there is no BNF grammar spec.

Four mermaid diagrams, each rendered and inspected before commit:

- the 20-pass pipeline coloured by what Lean actually proves about each pass —
  green proved, gold tracked `sorry`, blue partial, red not modelled. It replaces
  both ASCII blocks, and the backend column comes out uniformly red, which is the
  boundary argument as a picture;
- the closure as a heap tuple: header, code pointer, captured free variable, and
  the lifted top-level function that reads it back;
- Cheney two-space collection: root stack through `%r15`, forwarding, and the
  unreachable tuple left behind;
- the parallel-move clobber that made `add(20, 22)` return 40.

Diagrams use only the portable mermaid subset (quoted labels, no HTML, explicit
`classDef` colours) so they render on GitHub and survive the port.

ROADMAP.md still ticked as delivered the exact theorems the README describes as
vacuous or deleted — `closure_conversion_preserves_semantics`, the one the README
quotes as `∀ _p : Program, True := by trivial`, was checked off as a Phase 7
deliverable. Corrected: the four Phase 2 dataflow claims now say what those
theorems actually are (`liveness_sound` is an alias; there is no DSATUR in the
file); the removed and deleted theorems are marked as such with the reason;
`LimitFunctions.lean` is marked an identity stub; `RevealFunctions.lean` records
that it had not compiled since Phase 8. Also fixes a mis-citation that pointed the
GC heap-check bug at `012_proof_hygiene.md` instead of `005_tuples_gc.md`, and two
dangling cross-references.
