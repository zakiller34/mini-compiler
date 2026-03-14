import Lake
open Lake DSL

package «MiniCompiler» where
  leanOptions := #[⟨`autoImplicit, false⟩]

@[default_target]
lean_lib «MiniCompiler» where
  srcDir := "."

require mathlib from FileSystem.FilePath.mk "../../lean-proofs/mathlib4"
require cslib from FileSystem.FilePath.mk "../../lean-proofs/cslib"
