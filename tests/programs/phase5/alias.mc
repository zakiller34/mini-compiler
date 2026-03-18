// expect: 99
let v = vector(1, 2);
let w = v;
begin {
  w[0] = 99;
  v[0]
}
