// A tuple holding an Int, a Bool and a nested tuple at once.  Expect: 5
let t = vector(5, true, vector(1, 2));
if (t[1]) { t[0] } else { 0 }
