---
"mini-compiler": minor
---

Diagnostics now carry source positions.

Previously every parse and type error was a bare message with no indication of
where in the file it came from, which on a multi-hundred-line program means
reading the whole thing.

```
$ mc bad.mc -o bad.s
bad.mc:3:5: type error: +/- requires Int operands
```

- `Token` carries a `SourceLoc { line, col }`; the lexer tracks both, counting
  lines through comments and whitespace.
- Every parsed `Expr` is stamped with the position of the token that started it.
- `ParseError` and `TypeError` carry a `SourceLoc`, and the driver formats
  `file:line:col: kind: message`.

Known limitation, deliberately not papered over: passes rebuild nodes rather
than mutate them and do not propagate positions, so a node synthesised by a pass
has `line == 0` and is reported without a position rather than with a wrong one.
This affects `--dyn`, where type checking runs after `cast_insert`. Errors
detected in a continuation frame (after operands are already typed) report the
last operand entered, which points into the offending expression but not always
at its first token.
