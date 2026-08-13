// Closure over a MUTABLE captured variable (assignment conversion).  Expect: 12
let x = 0;
let bump = lambda (d:Int) : Int { begin { set! x (x + d); x } };
begin { let a = bump(5); let b = bump(7); x }
