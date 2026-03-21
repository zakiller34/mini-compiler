// is_even/is_odd mutual recursion
fn is_even(n: Int) : Int {
  if (n == 0) { 1 } else { is_odd(n - 1) }
}
fn is_odd(n: Int) : Int {
  if (n == 0) { 0 } else { is_even(n - 1) }
}
is_even(10) + is_odd(7)
