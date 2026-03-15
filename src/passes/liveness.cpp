#include "liveness.h"

#include <algorithm>
#include <map>

namespace {

void var_from_arg(const x86::Arg &a, std::set<std::string> &out) {
    if (const auto *v = std::get_if<x86::VarArg>(&a)) {
        out.insert(v->name);
    }
}

} // namespace

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
    }
    // SetCC, JmpIf, Jmp, Callq, Retq, Popq: no var reads
    return result;
}

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
    }
    // Cmpq, JmpIf, Jmp, Callq, Retq, Pushq: no var writes
    return result;
}

std::vector<std::set<std::string>>
analyze_liveness(const x86::Block &block) {
    auto n = block.instrs.size();
    std::vector<std::set<std::string>> live_after(n);
    if (n == 0) return live_after;

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
    for (size_t i = blk.instrs.size(); i > 0 && i > blk.instrs.size() - 2; --i) {
        const auto &instr = blk.instrs[i - 1];
        if (const auto *j = std::get_if<x86::Jmp>(&instr)) {
            succs.push_back(j->label);
        } else if (const auto *jc = std::get_if<x86::JmpIf>(&instr)) {
            succs.push_back(jc->label);
        }
    }
    return succs;
}

/// @brief Compute live_before for a block (live set at block entry)
static std::set<std::string>
block_live_before(const x86::Block &blk,
                  const std::vector<std::set<std::string>> &live_after) {
    if (blk.instrs.empty()) return {};
    auto w = instr_writes(blk.instrs[0]);
    auto r = instr_reads(blk.instrs[0]);
    std::set<std::string> lb = live_after[0];
    for (const auto &v : w) lb.erase(v);
    for (const auto &v : r) lb.insert(v);
    return lb;
}

std::map<std::string, std::vector<std::set<std::string>>>
analyze_liveness_program(const x86::X86Program &prog) {
    // Build successor map
    std::map<std::string, std::vector<std::string>> succ_map;
    for (const auto &[label, blk] : prog.blocks) {
        succ_map[label] = block_successors(blk);
    }

    // Collect all block labels (excluding main/conclusion which are structural)
    std::vector<std::string> block_order;
    for (const auto &[label, blk] : prog.blocks) {
        if (label != "main" && label != "conclusion") {
            block_order.push_back(label);
        }
    }
    // Sort in reverse alphabetical as approximation of reverse postorder
    std::sort(block_order.rbegin(), block_order.rend());

    // Initialize live-after for each block
    std::map<std::string, std::vector<std::set<std::string>>> result;
    std::map<std::string, std::set<std::string>> live_before_map;

    // Iterate to fixed point (for DAGs, 2 passes suffice)
    // invariant: live sets converge monotonically
    for (int pass = 0; pass < 3; ++pass) {
        for (const auto &label : block_order) {
            const auto &blk = prog.blocks.at(label);
            auto n = blk.instrs.size();
            if (n == 0) continue;

            // Compute live_after[last] = union of successors' live_before
            std::set<std::string> exit_live;
            for (const auto &succ : succ_map[label]) {
                auto it = live_before_map.find(succ);
                if (it != live_before_map.end()) {
                    exit_live.insert(it->second.begin(), it->second.end());
                }
            }

            // Run backward analysis with initial exit_live
            std::vector<std::set<std::string>> live_after(n);
            live_after[n - 1] = exit_live;
            for (auto i = static_cast<int64_t>(n) - 1; i >= 0; --i) {
                auto idx = static_cast<size_t>(i);
                auto w = instr_writes(blk.instrs[idx]);
                auto r = instr_reads(blk.instrs[idx]);
                std::set<std::string> lb = live_after[idx];
                for (const auto &v : w) lb.erase(v);
                for (const auto &v : r) lb.insert(v);
                if (i > 0) live_after[static_cast<size_t>(i - 1)] = lb;
                if (i == 0) live_before_map[label] = lb;
            }
            result[label] = std::move(live_after);
        }
    }
    return result;
}
