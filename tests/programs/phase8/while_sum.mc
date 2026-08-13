// Phase 4 while + set!, dynamically typed.  Expect: 55
let i = 1;
let acc = 0;
begin {
  while (i <= 10) { begin { set! acc (acc + i); set! i (i + 1) } };
  acc
}
