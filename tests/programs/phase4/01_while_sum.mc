let i = 0;
let sum = 0;
begin {
  while (i < 5) {
    begin {
      set! sum sum + i;
      set! i i + 1
    }
  };
  sum
}
