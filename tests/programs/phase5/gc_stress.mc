// expect: 0
// Allocates ~4000 two-element tuples against a 16 KiB heap, so the collector
// must run many times. Only the newest tuple is reachable at each step.
let n = 0;
let acc = vector(0, 0);
begin {
  while (n < 4000) {
    begin {
      set! acc vector(n, n);
      set! n n + 1
    }
  };
  acc[0] - 3999
}
