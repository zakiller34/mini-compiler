#include "assign_homes.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "graph_coloring.h"
#include "interference.h"
#include "liveness.h"

namespace {

x86::Arg replace_arg(const x86::Arg &a,
                     const std::map<std::string, x86::Arg> &homes) {
    if (const auto *v = std::get_if<x86::VarArg>(&a)) {
        return homes.at(v->name);
    }
    return a;
}

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
    return instr;
}

void collect_var_from_arg(const x86::Arg &a, std::set<std::string> &vars) {
    if (const auto *v = std::get_if<x86::VarArg>(&a)) {
        vars.insert(v->name);
    }
}

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
    }
}

const std::set<x86::Reg> callee_saved = {x86::Reg::Rbx, x86::Reg::R12,
                                          x86::Reg::R13, x86::Reg::R14};

} // namespace

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

    auto graph = build_interference(all_instrs, all_live_after);
    auto coloring = color_graph(graph, num_allocable_regs());

    // Collect all vars
    std::set<std::string> all_vars;
    for (const auto &[label, blk] : prog.blocks) {
        for (size_t j = 0; j < blk.instrs.size(); ++j) {
            collect_vars_instr(blk.instrs[j], all_vars);
        }
    }

    // Build homes
    std::map<std::string, x86::Arg> homes;
    std::set<x86::Reg> used_callee;
    int64_t spill_count = 0;

    for (const auto &[loc, color] : coloring) {
        if (const auto *name = std::get_if<std::string>(&loc)) {
            if (color < num_allocable_regs()) {
                auto reg = color_to_reg(color);
                homes[*name] = x86::RegArg{reg};
                if (callee_saved.count(reg) > 0) used_callee.insert(reg);
            } else {
                ++spill_count;
                homes[*name] = x86::Deref{x86::Reg::Rbp, -8 * spill_count};
            }
        }
    }

    for (const auto &var : all_vars) {
        if (homes.count(var) == 0) {
            homes[var] = x86::RegArg{color_to_reg(0)};
        }
    }

    int64_t stack_space = spill_count * 8;
    auto total_pushes = 1 + static_cast<int64_t>(used_callee.size());
    if (total_pushes % 2 == 0) {
        if (stack_space % 16 != 8) stack_space += 8;
    } else {
        if (stack_space % 16 != 0) stack_space += 8;
    }

    x86::X86Program result;
    result.stack_space = stack_space;
    result.used_callee_saved = used_callee;

    for (const auto &[label, blk] : prog.blocks) {
        x86::Block out_blk;
        for (size_t j = 0; j < blk.instrs.size(); ++j) {
            out_blk.instrs.push_back(replace_instr(blk.instrs[j], homes));
        }
        result.blocks[label] = std::move(out_blk);
    }
    return result;
}
