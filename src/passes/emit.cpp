#include "emit.h"

#include <string>
#include <vector>

namespace {

/// @brief Emit a single Arg in AT&T syntax
/// @ensures result is valid AT&T operand
std::string emit_arg(const x86::Arg &a) {
    if (const auto *imm = std::get_if<x86::Imm>(&a)) {
        return "$" + std::to_string(imm->value);
    }
    if (const auto *reg = std::get_if<x86::RegArg>(&a)) {
        return x86::reg_name(reg->reg);
    }
    if (const auto *deref = std::get_if<x86::Deref>(&a)) {
        return std::to_string(deref->offset) + "(" + x86::reg_name(deref->reg) + ")";
    }
    return "var:" + std::get<x86::VarArg>(a).name;
}

/// @brief Emit a single instruction as assembly text line
/// @ensures result is valid AT&T instruction
std::string emit_instr(const x86::Instr &i) {
    if (const auto *a = std::get_if<x86::Addq>(&i)) {
        return "    addq " + emit_arg(a->src) + ", " + emit_arg(a->dst);
    }
    if (const auto *s = std::get_if<x86::Subq>(&i)) {
        return "    subq " + emit_arg(s->src) + ", " + emit_arg(s->dst);
    }
    if (const auto *m = std::get_if<x86::Movq>(&i)) {
        return "    movq " + emit_arg(m->src) + ", " + emit_arg(m->dst);
    }
    if (const auto *n = std::get_if<x86::Negq>(&i)) {
        return "    negq " + emit_arg(n->dst);
    }
    if (const auto *p = std::get_if<x86::Pushq>(&i)) {
        return "    pushq " + emit_arg(p->src);
    }
    if (const auto *p = std::get_if<x86::Popq>(&i)) {
        return "    popq " + emit_arg(p->dst);
    }
    if (const auto *c = std::get_if<x86::Callq>(&i)) {
        return "    callq " + c->label;
    }
    if (std::holds_alternative<x86::Retq>(i)) {
        return "    retq";
    }
    return "    jmp " + std::get<x86::Jmp>(i).label;
}

/// @brief Emit a labeled block
/// @ensures result is "label:\n    instr\n..."
std::string emit_block(const std::string &label, const x86::Block &blk) {
    std::string result = label + ":\n";
    // invariant: result has instructions[0..i) emitted
    // decreases: blk.instrs.size() - i
    for (size_t i = 0; i < blk.instrs.size(); ++i) {
        result += emit_instr(blk.instrs[i]) + "\n";
    }
    return result;
}

} // namespace

/// @brief Emit full assembly: .globl main, then main/start/conclusion blocks
/// @requires prog has "main", "start", "conclusion" blocks
/// @ensures result is complete x86-64 AT&T assembly
std::string emit(const x86::X86Program &prog) {
    std::string result = "    .globl main\n";

    // Emit in fixed order: main, start, conclusion
    std::vector<std::string> order = {"main", "start", "conclusion"};

    // invariant: result has blocks order[0..i) emitted
    // decreases: order.size() - i
    for (size_t i = 0; i < order.size(); ++i) {
        auto it = prog.blocks.find(order[i]);
        if (it != prog.blocks.end()) {
            result += emit_block(it->first, it->second);
        }
    }
    return result;
}
