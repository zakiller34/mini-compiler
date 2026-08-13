import Lake
open Lake DSL

package «MiniCompiler» where
  leanOptions := #[⟨`autoImplicit, false⟩]

-- Fails the build on a theorem that proves nothing. See Hygiene.lean.
@[default_target]
lean_lib «Hygiene» where
  srcDir := "."

-- `globs` is load-bearing: without it Lake builds only the root module and
-- whatever it transitively imports, so a module missing from MiniCompiler.lean
-- is never elaborated and a hard compile error in it stays invisible.
@[default_target]
lean_lib «MiniCompiler» where
  srcDir := "."
  globs := #[.andSubmodules `MiniCompiler]
