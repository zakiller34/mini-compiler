#include "emit.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

std::string emit_arg(const x86::Arg &a) {
    if (const auto *imm = std::get_if<x86::Imm>(&a))
        return "$" + std::to_string(imm->value);
    if (const auto *reg = std::get_if<x86::RegArg>(&a))
        return x86::reg_name(reg->reg);
    if (const auto *deref = std::get_if<x86::Deref>(&a))
        return std::to_string(deref->offset) + "(" + x86::reg_name(deref->reg) + ")";
    if (const auto *ga = std::get_if<x86::GlobalArg>(&a))
        return ga->name + "(%rip)";
    return "var:" + std::get<x86::VarArg>(a).name;
}

/// @brief Emit arg as byte register (for setCC/movzbq src)
std::string emit_byte_arg(const x86::Arg &a) {
    if (const auto *reg = std::get_if<x86::RegArg>(&a))
        return x86::byte_reg_name(reg->reg);
    return emit_arg(a); // fallback for non-reg
}

std::string emit_instr(const x86::Instr &i) {
    if (const auto *a = std::get_if<x86::Addq>(&i))
        return "    addq " + emit_arg(a->src) + ", " + emit_arg(a->dst);
    if (const auto *s = std::get_if<x86::Subq>(&i))
        return "    subq " + emit_arg(s->src) + ", " + emit_arg(s->dst);
    if (const auto *m = std::get_if<x86::Movq>(&i))
        return "    movq " + emit_arg(m->src) + ", " + emit_arg(m->dst);
    if (const auto *n = std::get_if<x86::Negq>(&i))
        return "    negq " + emit_arg(n->dst);
    if (const auto *x = std::get_if<x86::Xorq>(&i))
        return "    xorq " + emit_arg(x->src) + ", " + emit_arg(x->dst);
    if (const auto *c = std::get_if<x86::Cmpq>(&i))
        return "    cmpq " + emit_arg(c->src) + ", " + emit_arg(c->dst);
    if (const auto *sc = std::get_if<x86::SetCC>(&i))
        return "    set" + x86::cc_name(sc->cc) + " " + emit_byte_arg(sc->dst);
    if (const auto *mz = std::get_if<x86::Movzbq>(&i))
        return "    movzbq " + emit_byte_arg(mz->src) + ", " + emit_arg(mz->dst);
    if (const auto *p = std::get_if<x86::Pushq>(&i))
        return "    pushq " + emit_arg(p->src);
    if (const auto *p = std::get_if<x86::Popq>(&i))
        return "    popq " + emit_arg(p->dst);
    if (const auto *c = std::get_if<x86::Callq>(&i))
        return "    callq " + c->label;
    if (std::holds_alternative<x86::Retq>(i))
        return "    retq";
    if (const auto *j = std::get_if<x86::JmpIf>(&i))
        return "    j" + x86::cc_name(j->cc) + " " + j->label;
    if (const auto *aq = std::get_if<x86::Andq>(&i))
        return "    andq " + emit_arg(aq->src) + ", " + emit_arg(aq->dst);
    if (const auto *sq = std::get_if<x86::Sarq>(&i))
        return "    sarq " + emit_arg(sq->src) + ", " + emit_arg(sq->dst);
    if (const auto *lq = std::get_if<x86::Leaq>(&i))
        return "    leaq " + emit_arg(lq->src) + ", " + emit_arg(lq->dst);
    return "    jmp " + std::get<x86::Jmp>(i).label;
}

std::string emit_block(const std::string &label, const x86::Block &blk) {
    std::string result = label + ":\n";
    for (size_t i = 0; i < blk.instrs.size(); ++i) {
        result += emit_instr(blk.instrs[i]) + "\n";
    }
    return result;
}

} // namespace

/// @brief Emit full assembly
/// @requires prog has "main" and "conclusion" blocks
/// @ensures result is complete x86-64 AT&T assembly
std::string emit(const x86::X86Program &prog) {
    std::string result = "    .globl main\n";

    // Emit main first
    auto it = prog.blocks.find("main");
    if (it != prog.blocks.end()) {
        result += emit_block("main", it->second);
    }

    // Emit all other blocks (sorted) except main and conclusion
    std::vector<std::string> labels;
    for (const auto &[label, blk] : prog.blocks) {
        if (label != "main" && label != "conclusion") {
            labels.push_back(label);
        }
    }
    std::sort(labels.begin(), labels.end());
    for (const auto &label : labels) {
        result += emit_block(label, prog.blocks.at(label));
    }

    // Emit conclusion last
    it = prog.blocks.find("conclusion");
    if (it != prog.blocks.end()) {
        result += emit_block("conclusion", it->second);
    }
    return result;
}
