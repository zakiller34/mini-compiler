// Runtime type dispatch: the same variable holds an Int or a Bool.  Expect: 1
fn describe(v) {
  if (integer?(v)) { 1 } else { if (boolean?(v)) { 2 } else { 3 } }
}
describe(99)
