#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "../ir/x86_ir.h"

namespace mc {

/// @brief Compute live-after sets for each instruction (single block)
std::vector<std::set<std::string>>
analyze_liveness(const x86::Block &block);

/// @brief Compute live-after sets for all blocks (multi-block CFG)
/// @requires prog has valid blocks with Jmp/JmpIf terminators
/// @ensures result[label][i] = vars live after instruction i in block label
std::map<std::string, std::vector<std::set<std::string>>>
analyze_liveness_program(const x86::X86Program &prog);

std::set<std::string> instr_reads(const x86::Instr &instr);
std::set<std::string> instr_writes(const x86::Instr &instr);

} // namespace mc
