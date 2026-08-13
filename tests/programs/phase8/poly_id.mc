// One identity function used at Int and at Bool: rejected by a static type
// checker, fine dynamically.  Expect: 9
fn id(v) { v }
if (id(true)) { id(9) } else { id(0) }
