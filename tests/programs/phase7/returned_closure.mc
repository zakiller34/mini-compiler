// Function returning a closure that captures its parameter.  Expect: 15
fn make_adder(n:Int) : (Int) -> Int {
  lambda (m:Int) : Int { m + n }
}
let add5 = make_adder(5);
add5(10)
