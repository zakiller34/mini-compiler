#include "assign_homes.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "graph_coloring.h"
#include "interference.h"
#include "liveness.h"

namespace mc {

namespace {

/// @brief Word size in bytes for stack slot layout
constexpr int64_t kWordSize = 8;

/// @brief Required stack alignment in bytes (System V AMD64 ABI)
constexpr int64_t kAlignment = 16;

x86::Arg replace_arg(const x86::Arg &a,
                     const std::map<std::string, x86::Arg> &homes) {
    if (const auto *v = std::get_if<x86::VarArg>(&a)) {
        return homes.at(v->name);
    }
    return a;
}

// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
x86::Instr replace_instr(const x86::Instr &instr,
                         const std::map<std::string, x86::Arg> &homes) {
    if (const auto *a = std::get_if<x86::Addq>(&instr))
        return x86::Addq{replace_arg(a->src, homes), replace_arg(a->dst, homes)};
    if (const auto *s = std::get_if<x86::Subq>(&instr))
        return x86::Subq{replace_arg(s->src, homes), replace_arg(s->dst, homes)};
    if (const auto *m = std::get_if<x86::Movq>(&instr))
        return x86::Movq{replace_arg(m->src, homes), replace_arg(m->dst, homes)};
    if (const auto *n = std::get_if<x86::Negq>(&instr))
        return x86::Negq{replace_arg(n->dst, homes)};
    if (const auto *x = std::get_if<x86::Xorq>(&instr))
        return x86::Xorq{replace_arg(x->src, homes), replace_arg(x->dst, homes)};
    if (const auto *c = std::get_if<x86::Cmpq>(&instr))
        return x86::Cmpq{replace_arg(c->src, homes), replace_arg(c->dst, homes)};
    if (const auto *sc = std::get_if<x86::SetCC>(&instr))
        return x86::SetCC{sc->cc, replace_arg(sc->dst, homes)};
    if (const auto *mz = std::get_if<x86::Movzbq>(&instr))
        return x86::Movzbq{replace_arg(mz->src, homes), replace_arg(mz->dst, homes)};
    if (const auto *p = std::get_if<x86::Pushq>(&instr))
        return x86::Pushq{replace_arg(p->src, homes)};
    if (const auto *p = std::get_if<x86::Popq>(&instr))
        return x86::Popq{replace_arg(p->dst, homes)};
    if (const auto *aq = std::get_if<x86::Andq>(&instr))
        return x86::Andq{replace_arg(aq->src, homes), replace_arg(aq->dst, homes)};
    if (const auto *sq = std::get_if<x86::Sarq>(&instr))
        return x86::Sarq{replace_arg(sq->src, homes), replace_arg(sq->dst, homes)};
    if (const auto *lq = std::get_if<x86::Leaq>(&instr))
        return x86::Leaq{replace_arg(lq->src, homes), replace_arg(lq->dst, homes)};
    if (const auto *ic = std::get_if<x86::IndirectCallq>(&instr))
        return x86::IndirectCallq{replace_arg(ic->func, homes), ic->arity};
    if (const auto *tj = std::get_if<x86::TailJmp>(&instr))
        return x86::TailJmp{replace_arg(tj->func, homes), tj->arity};
    if (const auto *oq = std::get_if<x86::Orq>(&instr))
        return x86::Orq{replace_arg(oq->src, homes), replace_arg(oq->dst, homes)};
    if (const auto *slq = std::get_if<x86::Salq>(&instr))
        return x86::Salq{replace_arg(slq->src, homes), replace_arg(slq->dst, homes)};
    if (const auto *im = std::get_if<x86::Imulq>(&instr))
        return x86::Imulq{replace_arg(im->src, homes), replace_arg(im->dst, homes)};
    return instr;
}

void collect_var_from_arg(const x86::Arg &a, std::set<std::string> &vars) {
    if (const auto *v = std::get_if<x86::VarArg>(&a)) {
        vars.insert(v->name);
    }
}

// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
void collect_vars_instr(const x86::Instr &instr, std::set<std::string> &vars) {
    if (const auto *a = std::get_if<x86::Addq>(&instr)) {
        collect_var_from_arg(a->src, vars); collect_var_from_arg(a->dst, vars);
    } else if (const auto *s = std::get_if<x86::Subq>(&instr)) {
        collect_var_from_arg(s->src, vars); collect_var_from_arg(s->dst, vars);
    } else if (const auto *m = std::get_if<x86::Movq>(&instr)) {
        collect_var_from_arg(m->src, vars); collect_var_from_arg(m->dst, vars);
    } else if (const auto *n = std::get_if<x86::Negq>(&instr)) {
        collect_var_from_arg(n->dst, vars);
    } else if (const auto *x = std::get_if<x86::Xorq>(&instr)) {
        collect_var_from_arg(x->src, vars); collect_var_from_arg(x->dst, vars);
    } else if (const auto *c = std::get_if<x86::Cmpq>(&instr)) {
        collect_var_from_arg(c->src, vars); collect_var_from_arg(c->dst, vars);
    } else if (const auto *sc = std::get_if<x86::SetCC>(&instr)) {
        collect_var_from_arg(sc->dst, vars);
    } else if (const auto *mz = std::get_if<x86::Movzbq>(&instr)) {
        collect_var_from_arg(mz->src, vars); collect_var_from_arg(mz->dst, vars);
    } else if (const auto *p = std::get_if<x86::Pushq>(&instr)) {
        collect_var_from_arg(p->src, vars);
    } else if (const auto *p = std::get_if<x86::Popq>(&instr)) {
        collect_var_from_arg(p->dst, vars);
    } else if (const auto *aq = std::get_if<x86::Andq>(&instr)) {
        collect_var_from_arg(aq->src, vars); collect_var_from_arg(aq->dst, vars);
    } else if (const auto *sq = std::get_if<x86::Sarq>(&instr)) {
        collect_var_from_arg(sq->src, vars); collect_var_from_arg(sq->dst, vars);
    } else if (const auto *lq = std::get_if<x86::Leaq>(&instr)) {
        collect_var_from_arg(lq->src, vars); collect_var_from_arg(lq->dst, vars);
    } else if (const auto *ic = std::get_if<x86::IndirectCallq>(&instr)) {
        collect_var_from_arg(ic->func, vars);
    } else if (const auto *tj = std::get_if<x86::TailJmp>(&instr)) {
        collect_var_from_arg(tj->func, vars);
    } else if (const auto *oq = std::get_if<x86::Orq>(&instr)) {
        collect_var_from_arg(oq->src, vars); collect_var_from_arg(oq->dst, vars);
    } else if (const auto *slq = std::get_if<x86::Salq>(&instr)) {
        collect_var_from_arg(slq->src, vars);
        collect_var_from_arg(slq->dst, vars);
    } else if (const auto *im = std::get_if<x86::Imulq>(&instr)) {
        collect_var_from_arg(im->src, vars); collect_var_from_arg(im->dst, vars);
    }
}

const std::set<x86::Reg> callee_saved = {x86::Reg::Rbx, x86::Reg::R12,
                                          x86::Reg::R13, x86::Reg::R14};

} // namespace

// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
x86::X86Program assign_homes(const x86::X86Program &prog) {
    // Multi-block liveness analysis
    auto all_liveness = analyze_liveness_program(prog);

    // Collect all instructions and live-after sets from all blocks
    std::vector<x86::Instr> all_instrs;
    std::vector<std::set<std::string>> all_live_after;

    for (const auto &[label, blk] : prog.blocks) {
        if (label == "main" || label == "conclusion") continue;
        auto it = all_liveness.find(label);
        if (it == all_liveness.end()) continue;
        const auto &live = it->second;
        for (size_t j = 0; j < blk.instrs.size(); ++j) {
            all_instrs.push_back(blk.instrs[j]);
            all_live_after.push_back(j < live.size() ? live[j]
                                                      : std::set<std::string>{});
        }
    }

    auto graph = build_interference(all_instrs, all_live_after,
                                     &prog.var_types);
    auto coloring = color_graph(graph, num_allocable_regs());

    // Collect all vars
    std::set<std::string> all_vars;
    for (const auto &[label, blk] : prog.blocks) {
        for (const auto &instr : blk.instrs) {
            collect_vars_instr(instr, all_vars);
        }
    }

    // Build homes
    std::map<std::string, x86::Arg> homes;
    std::set<x86::Reg> used_callee;
    int64_t spill_count = 0;

    int64_t root_spill_count = 0;
    for (const auto &[loc, color] : coloring) {
        if (const auto *name = std::get_if<std::string>(&loc)) {
            if (color < num_allocable_regs()) {
                auto reg = color_to_reg(color);
                homes[*name] = x86::RegArg{reg};
                if (callee_saved.count(reg) > 0) used_callee.insert(reg);
            } else {
                // Tuple- or Any-typed → root stack (R15), else regular stack
                auto vt = prog.var_types.find(*name);
                if (vt != prog.var_types.end() &&
                    is_root_type(vt->second)) {
                    homes[*name] = x86::Deref{x86::Reg::R15,
                                               kWordSize * root_spill_count};
                    ++root_spill_count;
                } else {
                    ++spill_count;
                    homes[*name] = x86::Deref{x86::Reg::Rbp,
                                               -kWordSize * spill_count};
                }
            }
        }
    }

    for (const auto &var : all_vars) {
        if (homes.count(var) == 0) {
            homes[var] = x86::RegArg{color_to_reg(0)};
        }
    }

    int64_t stack_space = spill_count * kWordSize;
    auto total_pushes = 1 + static_cast<int64_t>(used_callee.size());
    if (total_pushes % 2 == 0) {
        if (stack_space % kAlignment != kWordSize) stack_space += kWordSize;
    } else {
        if (stack_space % kAlignment != 0) stack_space += kWordSize;
    }

    x86::X86Program result;
    result.stack_space = stack_space;
    int64_t rss = root_spill_count * kWordSize;
    result.root_stack_space = (rss > prog.root_stack_space) ? rss : prog.root_stack_space;
    result.used_callee_saved = used_callee;
    result.var_types = prog.var_types;

    for (const auto &[label, blk] : prog.blocks) {
        x86::Block out_blk;
        for (const auto &instr : blk.instrs) {
            out_blk.instrs.push_back(replace_instr(instr, homes));
        }
        result.blocks[label] = std::move(out_blk);
    }

    // Process each function def independently
    // invariant: result.defs has assigned defs for prog.defs[0..i)
    for (const auto &xdef : prog.defs) {
        x86::X86Program tmp;
        tmp.blocks = xdef.blocks;
        tmp.var_types = xdef.var_types;
        auto assigned = assign_homes(tmp);
        x86::X86FunctionDef new_def;
        new_def.name = xdef.name;
        new_def.blocks = std::move(assigned.blocks);
        new_def.stack_space = assigned.stack_space;
        new_def.root_stack_space = assigned.root_stack_space;
        new_def.used_callee_saved = assigned.used_callee_saved;
        new_def.var_types = assigned.var_types;
        result.defs.push_back(std::move(new_def));
    }
    return result;
}

} // namespace mc
