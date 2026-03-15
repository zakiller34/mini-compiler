let i = 0;
let total = 0;
begin {
  while (i < 3) {
    let j = 0;
    begin {
      while (j < 3) {
        begin {
          set! total total + 1;
          set! j j + 1
        }
      };
      set! i i + 1
    }
  };
  total
}
