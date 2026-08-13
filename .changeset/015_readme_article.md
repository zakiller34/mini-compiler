---
"mini-compiler": patch
---

Rewrite README.md as a technical article on what the project's four checking
regimes actually caught, and what they missed.

Covers the machine (21 passes, 8 language phases, DSATUR allocation, Cheney GC,
closure conversion, 3-bit tags), then the audit: twenty vacuous Lean theorems
and the modelling bug they were hiding, a green build that was not building two
of its own modules, the `add(20, 22) == 40` miscompilation and why the caller
side was safe when the callee side was not, and the GC-check ordering bug that
writing tests for an untested component exposed.

Includes an exact proved / stated / not-modelled table, and states why semantics
preservation is out of reach without an operational semantics.
