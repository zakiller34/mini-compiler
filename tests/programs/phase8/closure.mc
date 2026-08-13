// Phase 7 closure over a mutable variable, dynamically typed.  Expect: 12
let x = 0;
let bump = lambda (d) { begin { set! x (x + d); x } };
begin { let a = bump(5); let b = bump(7); x }
