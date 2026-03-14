#include "patch_instructions.h"

#include <vector>

namespace {

/// @brief Check if an Arg is a memory reference (Deref)
/// @ensures result == true iff a is Deref
bool is_mem(const x86::Arg &a) { return std::holds_alternative<x86::Deref>(a); }

/// @brief Check if two Args are identical
/// @ensures result == true iff a and b represent same operand
bool args_equal(const x86::Arg &a, const x86::Arg &b) {
    if (const auto *da = std::get_if<x86::Deref>(&a)) {
        if (const auto *db = std::get_if<x86::Deref>(&b)) {
            return da->reg == db->reg && da->offset == db->offset;
        }
    }
    if (const auto *ra = std::get_if<x86::RegArg>(&a)) {
        if (const auto *rb = std::get_if<x86::RegArg>(&b)) {
            return ra->reg == rb->reg;
        }
    }
    if (const auto *ia = std::get_if<x86::Imm>(&a)) {
        if (const auto *ib = std::get_if<x86::Imm>(&b)) {
            return ia->value == ib->value;
        }
    }
    return false;
}

/// @brief Patch a single instruction, appending result(s) to out
/// @ensures out has patched instruction(s) with no two-mem-operand violations
void patch_one(const x86::Instr &instr, std::vector<x86::Instr> &out) {
    const x86::Arg rax = x86::RegArg{x86::Reg::Rax};

    if (const auto *m = std::get_if<x86::Movq>(&instr)) {
        if (args_equal(m->src, m->dst)) {
            return; // trivial move
        }
        if (is_mem(m->src) && is_mem(m->dst)) {
            out.push_back(x86::Movq{m->src, rax});
            out.push_back(x86::Movq{rax, m->dst});
            return;
        }
        out.push_back(instr);
    } else if (const auto *a = std::get_if<x86::Addq>(&instr)) {
        if (is_mem(a->src) && is_mem(a->dst)) {
            out.push_back(x86::Movq{a->src, rax});
            out.push_back(x86::Addq{rax, a->dst});
            return;
        }
        out.push_back(instr);
    } else if (const auto *s = std::get_if<x86::Subq>(&instr)) {
        if (is_mem(s->src) && is_mem(s->dst)) {
            out.push_back(x86::Movq{s->src, rax});
            out.push_back(x86::Subq{rax, s->dst});
            return;
        }
        out.push_back(instr);
    } else {
        out.push_back(instr);
    }
}

} // namespace

/// @brief Patch all instructions in program
/// @requires prog has no VarArg
/// @ensures no two-memory-operand instructions remain
x86::X86Program patch_instructions(const x86::X86Program &prog) {
    x86::X86Program result;
    result.stack_space = prog.stack_space;

    // invariant: result.blocks has all patched blocks processed so far
    // decreases: prog.blocks.end() - it
    for (auto it = prog.blocks.begin(); it != prog.blocks.end(); ++it) {
        std::vector<x86::Instr> patched;

        // invariant: patched has instructions for instrs[0..j)
        // decreases: it->second.instrs.size() - j
        for (size_t j = 0; j < it->second.instrs.size(); ++j) {
            patch_one(it->second.instrs[j], patched);
        }
        result.blocks[it->first] = x86::Block{std::move(patched)};
    }
    return result;
}
