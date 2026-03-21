// Tail-recursive sum with accumulator
fn sum_acc(n: Int, acc: Int) : Int {
  if (n == 0) { acc } else { sum_acc(n - 1, acc + n) }
}
sum_acc(100, 0)
