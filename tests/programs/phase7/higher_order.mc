// Passing a named function as a first-class value.  Expect: 12
fn inc(n:Int) : Int { n + 1 }
fn apply_twice(f: (Int) -> Int, x:Int) : Int { f(f(x)) }
apply_twice(inc, 10)
