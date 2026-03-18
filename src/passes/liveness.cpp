#include "liveness.h"

#include <algorithm>
#include <map>
#include <queue>

namespace {

/// @brief Extract var name from arg if it is a VarArg
/// @modifies out — inserts var name if present
void var_from_arg(const x86::Arg &a, std::set<std::string> &out) {
    if (const auto *v = std::get_if<x86::VarArg>(&a)) {
        out.insert(v->name);
    }
}

} // namespace

/// @brief Collect variables read by an x86 instruction
/// @ensures result contains all VarArg names in read positions
std::set<std::string> instr_reads(const x86::Instr &instr) {
    std::set<std::string> result;
    if (const auto *a = std::get_if<x86::Addq>(&instr)) {
        var_from_arg(a->src, result); var_from_arg(a->dst, result);
    } else if (const auto *s = std::get_if<x86::Subq>(&instr)) {
        var_from_arg(s->src, result); var_from_arg(s->dst, result);
    } else if (const auto *m = std::get_if<x86::Movq>(&instr)) {
        var_from_arg(m->src, result);
    } else if (const auto *n = std::get_if<x86::Negq>(&instr)) {
        var_from_arg(n->dst, result);
    } else if (const auto *x = std::get_if<x86::Xorq>(&instr)) {
        var_from_arg(x->src, result); var_from_arg(x->dst, result);
    } else if (const auto *c = std::get_if<x86::Cmpq>(&instr)) {
        var_from_arg(c->src, result); var_from_arg(c->dst, result);
    } else if (const auto *mz = std::get_if<x86::Movzbq>(&instr)) {
        var_from_arg(mz->src, result);
    } else if (const auto *p = std::get_if<x86::Pushq>(&instr)) {
        var_from_arg(p->src, result);
    } else if (const auto *aq = std::get_if<x86::Andq>(&instr)) {
        var_from_arg(aq->src, result); var_from_arg(aq->dst, result);
    } else if (const auto *sq = std::get_if<x86::Sarq>(&instr)) {
        var_from_arg(sq->src, result); var_from_arg(sq->dst, result);
    } else if (const auto *lq = std::get_if<x86::Leaq>(&instr)) {
        var_from_arg(lq->src, result);
    }
    // SetCC, JmpIf, Jmp, Callq, Retq, Popq: no var reads
    return result;
}

/// @brief Collect variables written by an x86 instruction
/// @ensures result contains all VarArg names in write positions
std::set<std::string> instr_writes(const x86::Instr &instr) {
    std::set<std::string> result;
    if (const auto *a = std::get_if<x86::Addq>(&instr)) {
        var_from_arg(a->dst, result);
    } else if (const auto *s = std::get_if<x86::Subq>(&instr)) {
        var_from_arg(s->dst, result);
    } else if (const auto *m = std::get_if<x86::Movq>(&instr)) {
        var_from_arg(m->dst, result);
    } else if (const auto *n = std::get_if<x86::Negq>(&instr)) {
        var_from_arg(n->dst, result);
    } else if (const auto *x = std::get_if<x86::Xorq>(&instr)) {
        var_from_arg(x->dst, result);
    } else if (const auto *sc = std::get_if<x86::SetCC>(&instr)) {
        var_from_arg(sc->dst, result);
    } else if (const auto *mz = std::get_if<x86::Movzbq>(&instr)) {
        var_from_arg(mz->dst, result);
    } else if (const auto *p = std::get_if<x86::Popq>(&instr)) {
        var_from_arg(p->dst, result);
    } else if (const auto *aq = std::get_if<x86::Andq>(&instr)) {
        var_from_arg(aq->dst, result);
    } else if (const auto *sq = std::get_if<x86::Sarq>(&instr)) {
        var_from_arg(sq->dst, result);
    } else if (const auto *lq = std::get_if<x86::Leaq>(&instr)) {
        var_from_arg(lq->dst, result);
    }
    // Cmpq, JmpIf, Jmp, Callq, Retq, Pushq: no var writes
    return result;
}

/// @brief Single-block liveness (backward pass, no CFG context)
std::vector<std::set<std::string>>
analyze_liveness(const x86::Block &block) {
    auto n = block.instrs.size();
    std::vector<std::set<std::string>> live_after(n);
    if (n == 0) return live_after;

    // decreases: i
    // invariant: live_after[i] correct for all i > current
    for (auto i = static_cast<int64_t>(n) - 1; i >= 0; --i) {
        auto idx = static_cast<size_t>(i);
        auto w = instr_writes(block.instrs[idx]);
        auto r = instr_reads(block.instrs[idx]);
        std::set<std::string> live_before = live_after[idx];
        for (const auto &v : w) live_before.erase(v);
        for (const auto &v : r) live_before.insert(v);
        if (i > 0) live_after[static_cast<size_t>(i - 1)] = live_before;
    }
    return live_after;
}

/// @brief Get successor block labels from a block's terminal instructions
static std::vector<std::string> block_successors(const x86::Block &blk) {
    std::vector<std::string> succs;
    if (blk.instrs.empty()) return succs;
    // Check last 1-2 instructions for jumps
    // invariant: succs has all jump targets from checked instructions
    size_t n = blk.instrs.size();
    size_t start = (n >= 2) ? n - 2 : 0;
    for (size_t i = start; i < n; ++i) {
        const auto &instr = blk.instrs[i];
        if (const auto *j = std::get_if<x86::Jmp>(&instr)) {
            succs.push_back(j->label);
        } else if (const auto *jc = std::get_if<x86::JmpIf>(&instr)) {
            succs.push_back(jc->label);
        }
    }
    return succs;
}

/// @brief Backward analysis for a single block given exit_live
/// @returns live_after vector and live_before (entry) set
static std::pair<std::vector<std::set<std::string>>, std::set<std::string>>
block_backward_pass(const x86::Block &blk,
                    const std::set<std::string> &exit_live) {
    auto n = blk.instrs.size();
    std::vector<std::set<std::string>> live_after(n);
    std::set<std::string> live_before;
    if (n == 0) return {live_after, live_before};

    live_after[n - 1] = exit_live;
    // decreases: i
    for (auto i = static_cast<int64_t>(n) - 1; i >= 0; --i) {
        auto idx = static_cast<size_t>(i);
        auto w = instr_writes(blk.instrs[idx]);
        auto r = instr_reads(blk.instrs[idx]);
        std::set<std::string> lb = live_after[idx];
        for (const auto &v : w) lb.erase(v);
        for (const auto &v : r) lb.insert(v);
        if (i > 0) {
            live_after[static_cast<size_t>(i - 1)] = lb;
        } else {
            live_before = lb;
        }
    }
    return {live_after, live_before};
}

/// @brief Build CFG successor/predecessor maps from x86 program
/// @requires prog.blocks valid, skips "main"/"conclusion"
/// @ensures succ_map[l] = successors, pred_map[l] = predecessors
struct CfgInfo {
    std::map<std::string, std::vector<std::string>> succ_map;
    std::map<std::string, std::vector<std::string>> pred_map;
    std::vector<std::string> block_order;
};

static CfgInfo build_cfg(const x86::X86Program &prog) {
    CfgInfo cfg;
    // invariant: succ_map/pred_map cover all non-structural blocks
    for (const auto &[label, blk] : prog.blocks) {
        if (label == "main" || label == "conclusion") continue;
        cfg.block_order.push_back(label);
        cfg.succ_map[label] = block_successors(blk);
        for (const auto &succ : cfg.succ_map[label]) {
            cfg.pred_map[succ].push_back(label);
        }
    }
    return cfg;
}

/// @brief Kildall's worklist algorithm for multi-block liveness
/// @requires prog has valid blocks with Jmp/JmpIf terminators
/// @ensures result[label][i] = vars live after instruction i; handles cycles
std::map<std::string, std::vector<std::set<std::string>>>
analyze_liveness_program(const x86::X86Program &prog) {
    auto cfg = build_cfg(prog);

    std::map<std::string, std::set<std::string>> live_before_map;
    std::map<std::string, std::vector<std::set<std::string>>> result;

    // invariant: live_before_map[l] ⊆ true live-before for l
    for (const auto &label : cfg.block_order) {
        live_before_map[label] = {};
    }

    std::set<std::string> worklist(cfg.block_order.begin(),
                                    cfg.block_order.end());

    // decreases: sum of |live_before[l]| is bounded by #vars × #blocks
    while (!worklist.empty()) {
        auto it = worklist.begin();
        std::string label = *it;
        worklist.erase(it);

        const auto &blk = prog.blocks.at(label);
        if (blk.instrs.empty()) continue;

        // Compute exit_live = union of successors' live_before
        std::set<std::string> exit_live;
        for (const auto &succ : cfg.succ_map[label]) {
            auto sit = live_before_map.find(succ);
            if (sit != live_before_map.end()) {
                exit_live.insert(sit->second.begin(), sit->second.end());
            }
        }

        auto [live_after, live_before] = block_backward_pass(blk, exit_live);
        result[label] = std::move(live_after);

        if (live_before != live_before_map[label]) {
            live_before_map[label] = std::move(live_before);
            auto pit = cfg.pred_map.find(label);
            if (pit != cfg.pred_map.end()) {
                for (const auto &pred : pit->second) {
                    worklist.insert(pred);
                }
            }
        }
    }
    return result;
}
