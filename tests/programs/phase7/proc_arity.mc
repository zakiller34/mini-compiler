// procedure_arity on a closure value.  Expect: 3
let f = lambda (a:Int, b:Int, c:Int) : Int { a + b + c };
procedure_arity(f)
