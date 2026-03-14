import Lake
open Lake DSL

package «MiniCompiler» where
  leanOptions := #[⟨`autoImplicit, false⟩]

@[default_target]
lean_lib «MiniCompiler» where
  srcDir := "."
