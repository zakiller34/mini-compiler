#pragma once

#include <set>
#include <string>
#include <vector>

#include "../ir/x86_ir.h"

/// @brief Compute live-after sets for each instruction (backward dataflow)
/// @requires block has valid x86 instrs (may contain VarArg)
/// @ensures result[i] = set of vars live after instruction i
/// @ensures result.size() == block.instrs.size()
std::vector<std::set<std::string>>
analyze_liveness(const x86::Block &block);

/// @brief Extract variable names read by an instruction
/// @ensures result contains all VarArg names in read positions
std::set<std::string> instr_reads(const x86::Instr &instr);

/// @brief Extract variable names written by an instruction
/// @ensures result contains all VarArg names in write positions
std::set<std::string> instr_writes(const x86::Instr &instr);
