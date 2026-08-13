---
"mini-compiler": minor
---

Phase 8: dynamic typing (L_Dyn / L_Any). A new `--dyn` flag compiles and
interprets annotation-free programs — `fn f(x) { ... }`, `lambda (x) { ... }`,
`t[i]` with a computed index — in which every value carries a 3-bit runtime
type tag (001 int, 100 bool, 010 tuple, 011 procedure, 101 void; 000 stays
reserved for untagged tuple pointers so the collector can tell the two apart).

Two new passes sit between `reveal_functions` and `convert_assignments`:
`cast_insert` compiles L_Dyn to L_Any by injecting literals and projecting
primitive operands, and `reveal_casts` lowers those casts to explicit tag
manipulation (`make-any`, `tag-of-any`, `value-of`) plus a `trapped-error`
exit. The statically typed language gains `Any` as a writable type together
with `inject`/`project` and the `integer?`/`boolean?`/`vector?`/`procedure?`/
`void?` predicates, so L_Any is usable directly.

Dynamic type errors — projecting a mismatched tag, indexing a tuple out of
bounds, calling a procedure with the wrong arity — halt with exit status 255,
and `mc -i --dyn` reports the same status, so interpreted and compiled runs
agree. The garbage collector now preserves a value's tag bits across a
collection and follows only tuple- and procedure-tagged pointers, and the
register allocator spills `Any` variables that are live across a call to the
root stack.

Also fixes two pre-existing bugs this exposed: `vector(a, b)` with variable
elements crashed `expose_allocation` (it now carries a variable-type
environment), and a `set!` whose value needed its own basic blocks silently
compiled to `0` in `explicate_control`.
