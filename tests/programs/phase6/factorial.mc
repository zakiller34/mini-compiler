// Sum from 0 to n (tests recursion)
fn sum_to(n: Int) : Int {
  if (n == 0) { 0 } else { n + sum_to(n - 1) }
}
sum_to(10)
