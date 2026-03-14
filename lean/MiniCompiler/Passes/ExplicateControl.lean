import MiniCompiler.AST

-- C_Var IR types
inductive Atom where
  | int (value : Int) : Atom
  | var (name : String) : Atom

inductive CExpr where
  | atom (a : Atom) : CExpr
  | read : CExpr
  | unary (op : UnaryOp) (a : Atom) : CExpr
  | binary (op : BinaryOp) (lhs rhs : Atom) : CExpr

def explicate_control (p : Program) : Unit := sorry
