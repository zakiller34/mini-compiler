#pragma once

#include <map>
#include <vector>

#include "interference.h"

namespace mc {

/// @brief Color interference graph using DSATUR with move biasing
/// @requires graph is valid undirected graph; Reg nodes pre-colored
/// @ensures no two adjacent nodes share a color
/// @ensures pre-colored (Reg) nodes keep their assigned colors
/// @ensures move biasing: prefer color of move-related neighbor when tied
std::map<Location, int> color_graph(const Graph &graph, int num_regs);

/// @brief Map from color index to allocable register
/// @requires color >= 0 && color < num_allocable_regs()
x86::Reg color_to_reg(int color);

/// @brief Number of allocable registers (11)
int num_allocable_regs();

/// @brief Get pre-assigned color for a register (-1 if not allocable)
int reg_to_color(x86::Reg r);

/// @brief Allocable registers in color order
const std::vector<x86::Reg> &allocable_regs();

} // namespace mc
