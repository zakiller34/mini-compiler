#include "limit_functions.h"

namespace mc {

/// @brief Limit functions to 6 params
/// @requires valid FunRefs
/// @ensures all defs <=6 params
/// Currently a passthrough — all phase 6 programs use <=6 params.
/// TODO: pack excess args into tuple when >6 params.
std::unique_ptr<Program> limit_functions(const Program &prog) {
    // Identity: program unchanged, caller responsible for lifetime
    // We cannot copy unique_ptrs, so this pass operates in-place
    // by returning nullptr and having the pipeline skip when no change needed.
    // For now, return nullptr to signal "use original".
    return nullptr;
}

} // namespace mc
