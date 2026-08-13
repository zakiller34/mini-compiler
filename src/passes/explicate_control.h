#pragma once

#include "../ast.h"
#include "../ir/c_ir.h"

#include <stdexcept>

namespace mc {

/// @brief Raised when explicate_control meets an expression its precondition
///        rules out — in practice, an operand RCO should have atomized.
class ExplicateError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// @brief Convert AST to C_Var IR with explicit control flow
/// @requires prog.body != nullptr (after shrink + uniquify + uncover_get + RCO)
/// @ensures result.blocks has "start" block; tails are Goto/IfStmt/Return
cir::CProgram explicate_control(const Program &prog);

} // namespace mc
