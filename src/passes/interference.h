#pragma once

#include <map>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "../ir/x86_ir.h"
#include "../type.h"

namespace mc {

/// Type-safe graph node: either a variable name or a register
using Location = std::variant<std::string, x86::Reg>;

struct Graph {
    std::set<Location> nodes;
    std::map<Location, std::set<Location>> adj;
    std::set<std::pair<Location, Location>> move_edges;

    /// @brief Add undirected interference edge between u and v
    /// @ensures u in adj[v] and v in adj[u]; no self-edges
    void add_edge(const Location &u, const Location &v);

    /// @brief Record a move-related pair (for move biasing)
    /// @ensures {u,v} in move_edges (canonical order)
    void add_move_edge(const Location &u, const Location &v);

    /// @brief Check if edge exists between u and v
    bool has_edge(const Location &u, const Location &v) const;
};

/// @brief Build interference graph from instructions + live-after sets
/// @requires live_after from analyze_liveness, instrs from same block
/// @ensures edge(u,v) iff u,v simultaneously live; movq special handling
/// @ensures callq: edges between live vars and caller-saved regs
/// @ensures a var of type Any live across a call interferes with every
///          allocable register, forcing it onto the root stack (Siek 9.9)
Graph build_interference(const std::vector<x86::Instr> &instrs,
                         const std::vector<std::set<std::string>> &live_after,
                         const std::map<std::string, TypePtr> *var_types
                             = nullptr);

/// Caller-saved registers that get clobbered by callq
const std::vector<x86::Reg> &caller_saved_regs();

} // namespace mc
