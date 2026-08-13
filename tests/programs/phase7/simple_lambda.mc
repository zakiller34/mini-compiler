// Anonymous function capturing an enclosing variable.  Expect: 8
let x = 5;
let f = lambda (y:Int) : Int { y + x };
f(3)
