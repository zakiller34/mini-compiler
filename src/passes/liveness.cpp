#include "liveness.h"

namespace {

/// @brief Extract VarArg name from an Arg if present
/// @ensures if a is VarArg, name added to out
void var_from_arg(const x86::Arg &a, std::set<std::string> &out) {
    if (const auto *v = std::get_if<x86::VarArg>(&a)) {
        out.insert(v->name);
    }
}

} // namespace

/// @brief Extract variable names read by an instruction
/// @ensures result contains all VarArg names in read positions
/// @invariant Addq/Subq read src AND dst (dst is read-modify-write)
/// @invariant Negq reads dst (read-modify-write)
/// @invariant Movq reads only src
std::set<std::string> instr_reads(const x86::Instr &instr) {
    std::set<std::string> result;
    if (const auto *a = std::get_if<x86::Addq>(&instr)) {
        var_from_arg(a->src, result);
        var_from_arg(a->dst, result); // read-modify-write
    } else if (const auto *s = std::get_if<x86::Subq>(&instr)) {
        var_from_arg(s->src, result);
        var_from_arg(s->dst, result); // read-modify-write
    } else if (const auto *m = std::get_if<x86::Movq>(&instr)) {
        var_from_arg(m->src, result);
    } else if (const auto *n = std::get_if<x86::Negq>(&instr)) {
        var_from_arg(n->dst, result); // read-modify-write
    } else if (const auto *p = std::get_if<x86::Pushq>(&instr)) {
        var_from_arg(p->src, result);
    }
    return result;
}

/// @brief Extract variable names written by an instruction
/// @ensures result contains all VarArg names in write positions
/// @invariant Movq/Addq/Subq/Negq write dst; Popq writes dst
/// @invariant Callq writes no vars (reg clobbers handled in interference)
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
    } else if (const auto *p = std::get_if<x86::Popq>(&instr)) {
        var_from_arg(p->dst, result);
    }
    return result;
}

/// @brief Compute live-after sets via backward dataflow
/// @requires block has valid x86 instrs
/// @ensures result[i] = set of vars live after instruction i
std::vector<std::set<std::string>>
analyze_liveness(const x86::Block &block) {
    auto n = block.instrs.size();
    std::vector<std::set<std::string>> live_after(n);

    if (n == 0) {
        return live_after;
    }

    // live_after[last] = {} (vars live after last instr = nothing for L_Var)
    // Walk backward: live_before[i] = (live_after[i] - writes(i)) | reads(i)
    //                live_after[i-1] = live_before[i]

    // invariant: live_after[i+1..n) computed correctly
    // decreases: i
    for (auto i = static_cast<int64_t>(n) - 1; i >= 0; --i) {
        auto idx = static_cast<size_t>(i);
        auto w = instr_writes(block.instrs[idx]);
        auto r = instr_reads(block.instrs[idx]);

        // live_before = (live_after - writes) | reads
        std::set<std::string> live_before = live_after[idx];
        for (const auto &v : w) {
            live_before.erase(v);
        }
        for (const auto &v : r) {
            live_before.insert(v);
        }

        // Propagate: live_after[i-1] = live_before[i]
        if (i > 0) {
            live_after[static_cast<size_t>(i - 1)] = live_before;
        }
    }

    return live_after;
}
