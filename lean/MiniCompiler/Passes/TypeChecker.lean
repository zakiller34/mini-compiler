import MiniCompiler.AST
import MiniCompiler.Passes.Tagging

/-!
# Type Checker — L_While

Type-check expressions with env. Handles while (Bool→Void),
set! (match→Void), begin (last type), void (Void).
-/

namespace MiniCompiler

abbrev TypeEnv := List (String × Ty)

/-- Look up variable type in environment. -/
def lookup (env : TypeEnv) (name : String) : Option Ty :=
  match env with
  | [] => none
  | (n, t) :: rest => if n == name then some t else lookup rest name

/-- Type-check an expression with environment. -/
def type_check_env : TypeEnv → Expr → Option Ty
  | _, .int _ => some .int
  | _, .bool _ => some .bool
  | env, .var n => lookup env n
  | _, .read => some .int
  | env, .unary .neg e => do
    let t ← type_check_env env e
    if t == .int then some .int else none
  | env, .unary .not e => do
    let t ← type_check_env env e
    if t == .bool then some .bool else none
  | env, .binary op lhs rhs => do
    let lt ← type_check_env env lhs
    let rt ← type_check_env env rhs
    match op with
    | .add | .sub =>
      if lt == .int && rt == .int then some .int else none
    | .and_ | .or_ =>
      if lt == .bool && rt == .bool then some .bool else none
    | .eq =>
      if lt == rt then some .bool else none
    | .lt | .le | .gt | .ge =>
      if lt == .int && rt == .int then some .bool else none
  | env, .if_ c t e => do
    let ct ← type_check_env env c
    if ct != .bool then none else do
    let tt ← type_check_env env t
    let et ← type_check_env env e
    if tt == et then some tt else none
  | env, .let_ v i b => do
    let it ← type_check_env env i
    type_check_env ((v, it) :: env) b
  | env, .while_ c body => do
    let ct ← type_check_env env c
    if ct != .bool then none else do
    let _ ← type_check_env env body
    some .void
  | env, .set_ v e => do
    let vt ← lookup env v
    let et ← type_check_env env e
    if vt == et then some .void else none
  | env, .begin es =>
    match es with
    | [] => some .void
    | [e] => type_check_env env e
    | e :: rest => do
      let _ ← type_check_env env e
      type_check_env env (.begin rest)
  | _, .void_ => some .void
  | env, .get n => lookup env n
  | env, .vector_ es => do
    let ts ← es.mapM (type_check_env env)
    some (.vector ts)
  | env, .vectorRef v i => do
    let vt ← type_check_env env v
    match vt with
    | .vector ts => ts[i]?
    | _ => none
  | env, .vectorSet v i e => do
    let vt ← type_check_env env v
    let et ← type_check_env env e
    match vt with
    | .vector ts => do
      let ti ← ts[i]?
      if ti == et then some .void else none
    | _ => none
  | env, .vectorLength v => do
    let vt ← type_check_env env v
    match vt with
    | .vector _ => some .int
    | _ => none
  | env, .apply f args => do
    let ft ← type_check_env env f
    match ft with
    | .fun ptypes ret =>
      if ptypes.length != args.length then none else do
      let atypes ← args.mapM (type_check_env env)
      if atypes == ptypes then some ret else none
    | _ => none
  | _, .funRef _ _ => none -- funRef needs env context, stub
  | env, .lambda ps rt b => do
    -- body checked in env extended with params; result is a function type
    let benv := ps.map (fun p => (p.1, p.2)) ++ env
    let bt ← type_check_env benv b
    if bt == rt then some (.fun (ps.map (·.2)) rt) else none
  | env, .procArity e => do
    let t ← type_check_env env e
    match t with
    | .fun _ _ => some .int
    | _ => none
  | _, .closure _ _ => none -- introduced post-typecheck, stub
  -- Phase 8 (L_Any): Siek 2023, figure 9.6
  | env, .inject e t => do
    let et ← type_check_env env e
    if isFlat t && et == t then some .any else none
  | env, .project e t => do
    let et ← type_check_env env e
    if isFlat t && et == .any then some t else none
  | env, .typePred _ e => do
    let et ← type_check_env env e
    if et == .any then some .bool else none
  | env, .anyVectorRef v i => do
    let vt ← type_check_env env v
    let it ← type_check_env env i
    if vt == .any && it == .int then some .any else none
  | env, .anyVectorSet v i x => do
    let vt ← type_check_env env v
    let it ← type_check_env env i
    let xt ← type_check_env env x
    if vt == .any && it == .int && xt == .any then some .void else none
  | env, .anyVectorLength v => do
    let vt ← type_check_env env v
    if vt == .any then some .int else none
  | env, .makeAny e _ => do
    let _ ← type_check_env env e
    some .any
  | env, .tagOfAny e => do
    let t ← type_check_env env e
    if t == .any then some .int else none
  | env, .valueOf e t => do
    let et ← type_check_env env e
    if et == .any then some t else none
  | _, .exit_ => some .void -- Exit halts; its type is never observed

/-- Top-level type check. -/
def type_check (e : Expr) : Option Ty := type_check_env [] e

-- NOTE: `type_progress` and `type_preservation` are deliberately absent.
--
-- Both previously stood here as `type_check e = some τ → True`, which asserts
-- nothing. Neither can even be *stated* without an operational semantics:
-- progress needs a small-step relation (`e ↦ e'`) and preservation needs the
-- notion of a value. This development has no evaluator — see the "What is and
-- is not proved" section of the README. Stating them honestly is a multi-week
-- project, and a vacuous stand-in is not a down payment on it.

/-- While always produces Void type. -/
theorem while_type_void : ∀ env c body τ,
    type_check_env env (.while_ c body) = some τ → τ = .void := by
  intro env c body τ h
  simp [type_check_env, Option.bind] at h
  split at h <;> simp_all
  split at h <;> simp_all

end MiniCompiler
