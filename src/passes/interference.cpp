#include "interference.h"

#include "graph_coloring.h"

namespace mc {

namespace {

/// Canonical pair ordering for move_edges
std::pair<Location, Location> canonical_pair(const Location &a,
                                             const Location &b) {
    return (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
}

} // namespace

void Graph::add_edge(const Location &u, const Location &v) {
    if (u == v) {
        return; // no self-edges
    }
    nodes.insert(u);
    nodes.insert(v);
    adj[u].insert(v);
    adj[v].insert(u);
}

void Graph::add_move_edge(const Location &u, const Location &v) {
    if (u == v) {
        return;
    }
    move_edges.insert(canonical_pair(u, v));
}

bool Graph::has_edge(const Location &u, const Location &v) const {
    auto it = adj.find(u);
    if (it == adj.end()) {
        return false;
    }
    return it->second.count(v) > 0;
}

const std::vector<x86::Reg> &caller_saved_regs() {
    static const std::vector<x86::Reg> regs = {
        x86::Reg::Rax, x86::Reg::Rcx,  x86::Reg::Rdx, x86::Reg::Rsi,
        x86::Reg::Rdi, x86::Reg::R8,   x86::Reg::R9,  x86::Reg::R10,
        x86::Reg::R11};
    return regs;
}

/// @brief Build interference graph
/// @requires live_after.size() == instrs.size()
/// @ensures edge(u,v) iff simultaneously live at some point
/// @ensures movq src,dst: no interference edge between src,dst; move_edge added
/// @ensures callq: edges between all live vars and caller-saved regs
// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
Graph build_interference(const std::vector<x86::Instr> &instrs,
                         const std::vector<std::set<std::string>> &live_after,
                         const std::map<std::string, TypePtr> *var_types) {
    Graph graph;

    // Add all vars as nodes
    // invariant: graph.nodes accumulates all vars seen in live_after sets
    for (size_t i = 0; i < live_after.size(); ++i) {
        for (const auto &v : live_after[i]) {
            graph.nodes.insert(Location{v});
        }
    }

    // invariant: edges added for instrs[0..k)
    // decreases: instrs.size() - k
    for (size_t k = 0; k < instrs.size(); ++k) {
        const auto &instr = instrs[k];
        const auto &live = live_after[k];

        if (const auto *m = std::get_if<x86::Movq>(&instr)) {
            // movq src, dst: add edges from dst to all live-after except dst
            // and src Special: no edge between src and dst (move-related)
            Location src_loc;
            if (const auto *sv = std::get_if<x86::VarArg>(&m->src)) {
                src_loc = Location{sv->name};
            } else if (const auto *sr = std::get_if<x86::RegArg>(&m->src)) {
                src_loc = Location{sr->reg};
            }

            Location dst_loc;
            bool has_dst = false;
            if (const auto *dv = std::get_if<x86::VarArg>(&m->dst)) {
                dst_loc = Location{dv->name};
                has_dst = true;
            } else if (const auto *dr = std::get_if<x86::RegArg>(&m->dst)) {
                dst_loc = Location{dr->reg};
                has_dst = true;
            }

            if (has_dst) {
                for (const auto &v : live) {
                    Location v_loc{v};
                    if (v_loc != dst_loc && v_loc != src_loc) {
                        graph.add_edge(dst_loc, v_loc);
                    }
                }
                // Record move edge for biasing
                if (src_loc != dst_loc) {
                    graph.add_move_edge(src_loc, dst_loc);
                }
            }
        } else if (std::holds_alternative<x86::Callq>(instr) ||
                   std::holds_alternative<x86::IndirectCallq>(instr)) {
            // Callq/IndirectCallq clobber caller-saved regs
            for (const auto &v : live) {
                Location v_loc{v};
                for (auto reg : caller_saved_regs()) {
                    graph.add_edge(v_loc, Location{reg});
                }
                // A live Any may hold a pointer the collector must see, so it
                // has to be spilled to the root stack rather than kept in a
                // register across the call (Siek 2023, section 9.9).
                if (var_types == nullptr) continue;
                auto it = var_types->find(v);
                if (it != var_types->end() && is_any_type(it->second)) {
                    for (auto reg : allocable_regs()) {
                        graph.add_edge(v_loc, Location{reg});
                    }
                }
            }
        } else {
            // General case: extract written VarArg dst, add edges to live
            const x86::Arg *dst_arg = nullptr;
            if (const auto *a = std::get_if<x86::Addq>(&instr)) {
                dst_arg = &a->dst;
            } else if (const auto *s = std::get_if<x86::Subq>(&instr)) {
                dst_arg = &s->dst;
            } else if (const auto *n = std::get_if<x86::Negq>(&instr)) {
                dst_arg = &n->dst;
            } else if (const auto *x = std::get_if<x86::Xorq>(&instr)) {
                dst_arg = &x->dst;
            } else if (const auto *aq = std::get_if<x86::Andq>(&instr)) {
                dst_arg = &aq->dst;
            } else if (const auto *sq = std::get_if<x86::Sarq>(&instr)) {
                dst_arg = &sq->dst;
            } else if (const auto *lq = std::get_if<x86::Leaq>(&instr)) {
                dst_arg = &lq->dst;
            } else if (const auto *oq = std::get_if<x86::Orq>(&instr)) {
                dst_arg = &oq->dst;
            } else if (const auto *slq = std::get_if<x86::Salq>(&instr)) {
                dst_arg = &slq->dst;
            } else if (const auto *im = std::get_if<x86::Imulq>(&instr)) {
                dst_arg = &im->dst;
            } else if (const auto *sc = std::get_if<x86::SetCC>(&instr)) {
                dst_arg = &sc->dst;
            } else if (const auto *mz = std::get_if<x86::Movzbq>(&instr)) {
                // Movzbq: like Movq (no edge between src and dst)
                Location src_loc;
                if (const auto *sv = std::get_if<x86::VarArg>(&mz->src)) {
                    src_loc = Location{sv->name};
                } else if (const auto *sr = std::get_if<x86::RegArg>(&mz->src)) {
                    src_loc = Location{sr->reg};
                }
                if (const auto *dv = std::get_if<x86::VarArg>(&mz->dst)) {
                    Location dst_loc{dv->name};
                    for (const auto &v : live) {
                        Location v_loc{v};
                        if (v_loc != dst_loc && v_loc != src_loc) {
                            graph.add_edge(dst_loc, v_loc);
                        }
                    }
                }
                dst_arg = nullptr; // already handled
            }
            if (dst_arg != nullptr) {
                if (const auto *dv = std::get_if<x86::VarArg>(dst_arg)) {
                    Location dst_loc{dv->name};
                    for (const auto &v : live) {
                        Location v_loc{v};
                        if (v_loc != dst_loc) {
                            graph.add_edge(dst_loc, v_loc);
                        }
                    }
                }
            }
            // Cmpq, JmpIf, Jmp, Pushq, Popq, Retq: no var writes
        }
    }

    return graph;
}

} // namespace mc
