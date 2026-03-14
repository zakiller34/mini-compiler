#include "x86_ir.h"

#include <string>

namespace x86 {

/// @brief Map register enum to AT&T name
/// @ensures result starts with '%'
std::string reg_name(Reg r) {
    switch (r) {
    case Reg::Rsp: return "%rsp";
    case Reg::Rbp: return "%rbp";
    case Reg::Rax: return "%rax";
    case Reg::Rbx: return "%rbx";
    case Reg::Rcx: return "%rcx";
    case Reg::Rdx: return "%rdx";
    case Reg::Rsi: return "%rsi";
    case Reg::Rdi: return "%rdi";
    case Reg::R8: return "%r8";
    case Reg::R9: return "%r9";
    case Reg::R10: return "%r10";
    case Reg::R11: return "%r11";
    case Reg::R12: return "%r12";
    case Reg::R13: return "%r13";
    case Reg::R14: return "%r14";
    case Reg::R15: return "%r15";
    }
    return "%unknown";
}

/// @brief Dump an Arg in AT&T syntax
/// @ensures result is valid AT&T operand string
std::string dump_arg(const Arg &a) {
    if (const auto *imm = std::get_if<Imm>(&a)) {
        return "$" + std::to_string(imm->value);
    }
    if (const auto *reg = std::get_if<RegArg>(&a)) {
        return reg_name(reg->reg);
    }
    if (const auto *deref = std::get_if<Deref>(&a)) {
        return std::to_string(deref->offset) + "(" + reg_name(deref->reg) + ")";
    }
    return "var:" + std::get<VarArg>(a).name;
}

/// @brief Dump a single instruction in AT&T syntax
/// @ensures result is valid AT&T instruction line
std::string dump_instr(const Instr &i) {
    if (const auto *a = std::get_if<Addq>(&i)) {
        return "    addq " + dump_arg(a->src) + ", " + dump_arg(a->dst);
    }
    if (const auto *s = std::get_if<Subq>(&i)) {
        return "    subq " + dump_arg(s->src) + ", " + dump_arg(s->dst);
    }
    if (const auto *m = std::get_if<Movq>(&i)) {
        return "    movq " + dump_arg(m->src) + ", " + dump_arg(m->dst);
    }
    if (const auto *n = std::get_if<Negq>(&i)) {
        return "    negq " + dump_arg(n->dst);
    }
    if (const auto *p = std::get_if<Pushq>(&i)) {
        return "    pushq " + dump_arg(p->src);
    }
    if (const auto *p = std::get_if<Popq>(&i)) {
        return "    popq " + dump_arg(p->dst);
    }
    if (const auto *c = std::get_if<Callq>(&i)) {
        return "    callq " + c->label;
    }
    if (std::holds_alternative<Retq>(i)) {
        return "    retq";
    }
    return "    jmp " + std::get<Jmp>(i).label;
}

/// @brief Dump entire X86Program in AT&T syntax
/// @ensures result contains all blocks as label: instrs
std::string X86Program::dump() const {
    std::string result;

    // invariant: result accumulates all processed blocks
    // decreases: blocks.end() - it
    for (auto it = blocks.begin(); it != blocks.end(); ++it) {
        result += it->first + ":\n";
        const auto &blk = it->second;

        // invariant: instrs[0..j) dumped
        // decreases: blk.instrs.size() - j
        for (size_t j = 0; j < blk.instrs.size(); ++j) {
            result += dump_instr(blk.instrs[j]) + "\n";
        }
    }
    return result;
}

} // namespace x86
