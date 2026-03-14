#include "assign_homes.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "graph_coloring.h"
#include "interference.h"
#include "liveness.h"

namespace {

/// @brief Replace VarArg in an Arg with its assigned home (reg or stack)
/// @ensures result has no VarArg if var is in homes
x86::Arg replace_arg(const x86::Arg &a,
                     const std::map<std::string, x86::Arg> &homes) {
    if (const auto *v = std::get_if<x86::VarArg>(&a)) {
        return homes.at(v->name);
    }
    return a;
}

/// @brief Replace VarArgs in a single instruction
/// @ensures result instruction has no VarArg
x86::Instr replace_instr(const x86::Instr &instr,
                         const std::map<std::string, x86::Arg> &homes) {
    if (const auto *a = std::get_if<x86::Addq>(&instr)) {
        return x86::Addq{replace_arg(a->src, homes),
                         replace_arg(a->dst, homes)};
    }
    if (const auto *s = std::get_if<x86::Subq>(&instr)) {
        return x86::Subq{replace_arg(s->src, homes),
                         replace_arg(s->dst, homes)};
    }
    if (const auto *m = std::get_if<x86::Movq>(&instr)) {
        return x86::Movq{replace_arg(m->src, homes),
                         replace_arg(m->dst, homes)};
    }
    if (const auto *n = std::get_if<x86::Negq>(&instr)) {
        return x86::Negq{replace_arg(n->dst, homes)};
    }
    if (const auto *p = std::get_if<x86::Pushq>(&instr)) {
        return x86::Pushq{replace_arg(p->src, homes)};
    }
    if (const auto *p = std::get_if<x86::Popq>(&instr)) {
        return x86::Popq{replace_arg(p->dst, homes)};
    }
    return instr;
}

/// Callee-saved registers that require save/restore in prelude/conclusion
const std::set<x86::Reg> callee_saved = {x86::Reg::Rbx, x86::Reg::R12,
                                          x86::Reg::R13, x86::Reg::R14};

} // namespace

/// @brief Assign homes using graph-coloring register allocation
/// @requires prog has pseudo-x86 blocks with VarArg
/// @ensures no VarArg remains, stack_space is 16-aligned
/// @ensures used_callee_saved populated with callee-saved regs in use
x86::X86Program assign_homes(const x86::X86Program &prog) {
    // Collect all instructions from all blocks for liveness + interference
    // For L_Var (Phase 2) there's only one "start" block
    std::vector<x86::Instr> all_instrs;
    std::vector<std::set<std::string>> all_live_after;

    // invariant: all_instrs/all_live_after accumulate data from processed
    // blocks decreases: prog.blocks.end() - it
    for (auto it = prog.blocks.begin(); it != prog.blocks.end(); ++it) {
        auto live = analyze_liveness(it->second);
        // invariant: instrs[0..j) and live[0..j) appended
        for (size_t j = 0; j < it->second.instrs.size(); ++j) {
            all_instrs.push_back(it->second.instrs[j]);
            all_live_after.push_back(live[j]);
        }
    }

    // Build interference graph and color it
    auto graph = build_interference(all_instrs, all_live_after);
    auto coloring = color_graph(graph, num_allocable_regs());

    // Collect all variable names (including dead vars not in graph)
    std::set<std::string> all_vars;
    for (auto it = prog.blocks.begin(); it != prog.blocks.end(); ++it) {
        for (size_t j = 0; j < it->second.instrs.size(); ++j) {
            const auto &instr = it->second.instrs[j];
            auto collect = [&](const x86::Arg &a) {
                if (const auto *v = std::get_if<x86::VarArg>(&a)) {
                    all_vars.insert(v->name);
                }
            };
            if (const auto *a = std::get_if<x86::Addq>(&instr)) {
                collect(a->src); collect(a->dst);
            } else if (const auto *s = std::get_if<x86::Subq>(&instr)) {
                collect(s->src); collect(s->dst);
            } else if (const auto *m = std::get_if<x86::Movq>(&instr)) {
                collect(m->src); collect(m->dst);
            } else if (const auto *n = std::get_if<x86::Negq>(&instr)) {
                collect(n->dst);
            } else if (const auto *p = std::get_if<x86::Pushq>(&instr)) {
                collect(p->src);
            } else if (const auto *p = std::get_if<x86::Popq>(&instr)) {
                collect(p->dst);
            }
        }
    }

    // Build homes map: variable name -> Arg (RegArg or Deref)
    std::map<std::string, x86::Arg> homes;
    std::set<x86::Reg> used_callee;
    int64_t spill_count = 0;

    // invariant: homes has mapping for all vars processed so far
    for (const auto &[loc, color] : coloring) {
        if (const auto *name = std::get_if<std::string>(&loc)) {
            if (color < num_allocable_regs()) {
                auto reg = color_to_reg(color);
                homes[*name] = x86::RegArg{reg};
                if (callee_saved.count(reg) > 0) {
                    used_callee.insert(reg);
                }
            } else {
                ++spill_count;
                homes[*name] =
                    x86::Deref{x86::Reg::Rbp, -8 * spill_count};
            }
        }
    }

    // Dead vars (in instructions but never live): assign to lowest color
    for (const auto &var : all_vars) {
        if (homes.count(var) == 0) {
            // Dead var: assign to first available reg (won't conflict)
            homes[var] = x86::RegArg{color_to_reg(0)};
        }
    }

    // Compute stack space (spills only; callee-saved handled by push/pop)
    int64_t stack_space = spill_count * 8;
    // Alignment: at main entry rsp ≡ 8 (mod 16) per ABI.
    // After total_pushes pushes: rsp ≡ 8 - (total_pushes * 8) (mod 16).
    //   Even pushes: rsp ≡ 8 (mod 16). Need stack_space ≡ 8 (mod 16).
    //   Odd pushes:  rsp ≡ 0 (mod 16). Need stack_space ≡ 0 (mod 16).
    // This ensures rsp ≡ 0 (mod 16) before any callq.
    auto total_pushes =
        1 + static_cast<int64_t>(used_callee.size());
    if (total_pushes % 2 == 0) {
        // Even pushes: need stack_space ≡ 8 (mod 16)
        if (stack_space % 16 != 8) {
            stack_space += 8;
        }
    } else {
        // Odd pushes: need stack_space ≡ 0 (mod 16)
        if (stack_space % 16 != 0) {
            stack_space += 8;
        }
    }

    // Replace VarArgs in all blocks
    x86::X86Program result;
    result.stack_space = stack_space;
    result.used_callee_saved = used_callee;

    // invariant: result.blocks has all processed blocks
    // decreases: prog.blocks.end() - it
    for (auto it = prog.blocks.begin(); it != prog.blocks.end(); ++it) {
        x86::Block blk;
        // invariant: blk.instrs has replaced instrs[0..j)
        // decreases: it->second.instrs.size() - j
        for (size_t j = 0; j < it->second.instrs.size(); ++j) {
            blk.instrs.push_back(replace_instr(it->second.instrs[j], homes));
        }
        result.blocks[it->first] = std::move(blk);
    }
    return result;
}
