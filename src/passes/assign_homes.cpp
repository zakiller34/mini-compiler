#include "assign_homes.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

/// @brief Collect all variable names from an Arg
/// @ensures vars contains all VarArg names found in a
void collect_vars_arg(const x86::Arg &a, std::set<std::string> &vars) {
    if (const auto *v = std::get_if<x86::VarArg>(&a)) {
        vars.insert(v->name);
    }
}

/// @brief Collect all variable names from an instruction
/// @ensures vars contains all VarArg names in instr
void collect_vars_instr(const x86::Instr &instr, std::set<std::string> &vars) {
    if (const auto *a = std::get_if<x86::Addq>(&instr)) {
        collect_vars_arg(a->src, vars);
        collect_vars_arg(a->dst, vars);
    } else if (const auto *s = std::get_if<x86::Subq>(&instr)) {
        collect_vars_arg(s->src, vars);
        collect_vars_arg(s->dst, vars);
    } else if (const auto *m = std::get_if<x86::Movq>(&instr)) {
        collect_vars_arg(m->src, vars);
        collect_vars_arg(m->dst, vars);
    } else if (const auto *n = std::get_if<x86::Negq>(&instr)) {
        collect_vars_arg(n->dst, vars);
    } else if (const auto *p = std::get_if<x86::Pushq>(&instr)) {
        collect_vars_arg(p->src, vars);
    } else if (const auto *p = std::get_if<x86::Popq>(&instr)) {
        collect_vars_arg(p->dst, vars);
    }
}

/// @brief Replace VarArg in an Arg with its stack location
/// @ensures result has no VarArg if var is in homes
x86::Arg replace_arg(const x86::Arg &a, const std::map<std::string, int64_t> &homes) {
    if (const auto *v = std::get_if<x86::VarArg>(&a)) {
        return x86::Deref{x86::Reg::Rbp, homes.at(v->name)};
    }
    return a;
}

/// @brief Replace VarArgs in a single instruction
/// @ensures result instruction has no VarArg
x86::Instr replace_instr(const x86::Instr &instr, const std::map<std::string, int64_t> &homes) {
    if (const auto *a = std::get_if<x86::Addq>(&instr)) {
        return x86::Addq{replace_arg(a->src, homes), replace_arg(a->dst, homes)};
    }
    if (const auto *s = std::get_if<x86::Subq>(&instr)) {
        return x86::Subq{replace_arg(s->src, homes), replace_arg(s->dst, homes)};
    }
    if (const auto *m = std::get_if<x86::Movq>(&instr)) {
        return x86::Movq{replace_arg(m->src, homes), replace_arg(m->dst, homes)};
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

} // namespace

/// @brief Assign stack homes to all variables
/// @requires prog has pseudo-x86 blocks with VarArg
/// @ensures no VarArg remains, stack_space is 16-aligned
x86::X86Program assign_homes(const x86::X86Program &prog) {
    std::set<std::string> vars;

    // invariant: vars accumulates all variable names seen
    // decreases: prog.blocks.end() - it
    for (auto it = prog.blocks.begin(); it != prog.blocks.end(); ++it) {
        // invariant: vars has all vars from instrs[0..j)
        // decreases: it->second.instrs.size() - j
        for (size_t j = 0; j < it->second.instrs.size(); ++j) {
            collect_vars_instr(it->second.instrs[j], vars);
        }
    }

    std::map<std::string, int64_t> homes;
    int64_t offset = -8;
    // invariant: each var gets unique offset, offset decreases by 8
    // decreases: vars.end() - vit
    for (auto vit = vars.begin(); vit != vars.end(); ++vit) {
        homes[*vit] = offset;
        offset -= 8;
    }

    auto num_vars = static_cast<int64_t>(vars.size());
    int64_t stack_space = num_vars * 8;
    if (stack_space % 16 != 0) {
        stack_space += 8;
    }

    x86::X86Program result;
    result.stack_space = stack_space;

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
