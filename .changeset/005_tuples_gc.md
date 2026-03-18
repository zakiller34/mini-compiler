---
"mini-compiler": minor
---

Phase 5: tuples & garbage collection. Heap-allocated fixed-length heterogeneous tuples with `vector(e1,e2)`, `v[i]`, `v[i]=val`, `length(v)`. Two-space Cheney copying GC in runtime. New `expose_allocation` pass lowers vector construction to allocate+collect+init. Register allocator routes tuple-typed spills to root stack (R15). Parameterized type system replaces enum Type.
