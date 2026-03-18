#include "x86_ir.h"

#include <string>

namespace x86 {

std::string reg_name(Reg r) {
    switch (r) {
    case Reg::Rsp: return "%rsp"; case Reg::Rbp: return "%rbp";
    case Reg::Rax: return "%rax"; case Reg::Rbx: return "%rbx";
    case Reg::Rcx: return "%rcx"; case Reg::Rdx: return "%rdx";
    case Reg::Rsi: return "%rsi"; case Reg::Rdi: return "%rdi";
    case Reg::R8: return "%r8";   case Reg::R9: return "%r9";
    case Reg::R10: return "%r10"; case Reg::R11: return "%r11";
    case Reg::R12: return "%r12"; case Reg::R13: return "%r13";
    case Reg::R14: return "%r14"; case Reg::R15: return "%r15";
    }
    return "%unknown";
}

std::string byte_reg_name(Reg r) {
    switch (r) {
    case Reg::Rax: return "%al";  case Reg::Rbx: return "%bl";
    case Reg::Rcx: return "%cl";  case Reg::Rdx: return "%dl";
    case Reg::Rsi: return "%sil"; case Reg::Rdi: return "%dil";
    case Reg::Rbp: return "%bpl"; case Reg::Rsp: return "%spl";
    case Reg::R8: return "%r8b";  case Reg::R9: return "%r9b";
    case Reg::R10: return "%r10b"; case Reg::R11: return "%r11b";
    case Reg::R12: return "%r12b"; case Reg::R13: return "%r13b";
    case Reg::R14: return "%r14b"; case Reg::R15: return "%r15b";
    }
    return "%unknown";
}

std::string cc_name(CC cc) {
    switch (cc) {
    case CC::E: return "e";   case CC::NE: return "ne";
    case CC::L: return "l";   case CC::LE: return "le";
    case CC::G: return "g";   case CC::GE: return "ge";
    }
    return "?";
}

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
    if (const auto *ga = std::get_if<GlobalArg>(&a)) {
        return ga->name + "(%rip)";
    }
    return "var:" + std::get<VarArg>(a).name;
}

std::string dump_instr(const Instr &i) {
    if (const auto *a = std::get_if<Addq>(&i))
        return "    addq " + dump_arg(a->src) + ", " + dump_arg(a->dst);
    if (const auto *s = std::get_if<Subq>(&i))
        return "    subq " + dump_arg(s->src) + ", " + dump_arg(s->dst);
    if (const auto *m = std::get_if<Movq>(&i))
        return "    movq " + dump_arg(m->src) + ", " + dump_arg(m->dst);
    if (const auto *n = std::get_if<Negq>(&i))
        return "    negq " + dump_arg(n->dst);
    if (const auto *x = std::get_if<Xorq>(&i))
        return "    xorq " + dump_arg(x->src) + ", " + dump_arg(x->dst);
    if (const auto *c = std::get_if<Cmpq>(&i))
        return "    cmpq " + dump_arg(c->src) + ", " + dump_arg(c->dst);
    if (const auto *sc = std::get_if<SetCC>(&i))
        return "    set" + cc_name(sc->cc) + " " + dump_arg(sc->dst);
    if (const auto *mz = std::get_if<Movzbq>(&i))
        return "    movzbq " + dump_arg(mz->src) + ", " + dump_arg(mz->dst);
    if (const auto *p = std::get_if<Pushq>(&i))
        return "    pushq " + dump_arg(p->src);
    if (const auto *p = std::get_if<Popq>(&i))
        return "    popq " + dump_arg(p->dst);
    if (const auto *c = std::get_if<Callq>(&i))
        return "    callq " + c->label;
    if (std::holds_alternative<Retq>(i))
        return "    retq";
    if (const auto *j = std::get_if<JmpIf>(&i))
        return "    j" + cc_name(j->cc) + " " + j->label;
    if (const auto *aq = std::get_if<Andq>(&i))
        return "    andq " + dump_arg(aq->src) + ", " + dump_arg(aq->dst);
    if (const auto *sq = std::get_if<Sarq>(&i))
        return "    sarq " + dump_arg(sq->src) + ", " + dump_arg(sq->dst);
    if (const auto *lq = std::get_if<Leaq>(&i))
        return "    leaq " + dump_arg(lq->src) + ", " + dump_arg(lq->dst);
    return "    jmp " + std::get<Jmp>(i).label;
}

std::string X86Program::dump() const {
    std::string result;
    for (auto it = blocks.begin(); it != blocks.end(); ++it) {
        result += it->first + ":\n";
        for (size_t j = 0; j < it->second.instrs.size(); ++j) {
            result += dump_instr(it->second.instrs[j]) + "\n";
        }
    }
    return result;
}

} // namespace x86
