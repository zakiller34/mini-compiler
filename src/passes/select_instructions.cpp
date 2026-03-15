#include "select_instructions.h"

#include <vector>

namespace {

x86::Arg atom_to_arg(const cir::Atom &a) {
    if (const auto *i = std::get_if<cir::IntAtom>(&a)) {
        return x86::Imm{i->value};
    }
    if (const auto *b = std::get_if<cir::BoolAtom>(&a)) {
        return x86::Imm{b->value ? 1 : 0};
    }
    return x86::VarArg{std::get<cir::VarAtom>(a).name};
}

x86::CC cmp_to_cc(cir::CCmpOp op) {
    switch (op) {
    case cir::CCmpOp::Eq: return x86::CC::E;
    case cir::CCmpOp::Lt: return x86::CC::L;
    case cir::CCmpOp::Le: return x86::CC::LE;
    case cir::CCmpOp::Gt: return x86::CC::G;
    case cir::CCmpOp::Ge: return x86::CC::GE;
    }
    return x86::CC::E;
}

/// @brief Emit instructions for CExpr, storing result in dst
void emit_cexpr(const cir::CExpr &expr, const x86::Arg &dst,
                std::vector<x86::Instr> &instrs) {
    if (const auto *ae = std::get_if<cir::AtomExpr>(&expr)) {
        instrs.push_back(x86::Movq{atom_to_arg(ae->atom), dst});
    } else if (std::holds_alternative<cir::CReadExpr>(expr)) {
        instrs.push_back(x86::Callq{"read_int", 0});
        instrs.push_back(x86::Movq{x86::RegArg{x86::Reg::Rax}, dst});
    } else if (const auto *ue = std::get_if<cir::CUnaryExpr>(&expr)) {
        instrs.push_back(x86::Movq{atom_to_arg(ue->operand), dst});
        instrs.push_back(x86::Negq{dst});
    } else if (const auto *be = std::get_if<cir::CBinaryExpr>(&expr)) {
        instrs.push_back(x86::Movq{atom_to_arg(be->lhs), dst});
        x86::Arg rhs_arg = atom_to_arg(be->rhs);
        if (be->op == cir::CBinaryOp::Add) {
            instrs.push_back(x86::Addq{rhs_arg, dst});
        } else {
            instrs.push_back(x86::Subq{rhs_arg, dst});
        }
    } else if (const auto *ce = std::get_if<cir::CCmpExpr>(&expr)) {
        // cmpq rhs, lhs; setCC %al; movzbq %al, dst
        instrs.push_back(x86::Cmpq{atom_to_arg(ce->rhs),
                                     atom_to_arg(ce->lhs)});
        x86::Arg al = x86::RegArg{x86::Reg::Rax};
        instrs.push_back(x86::SetCC{cmp_to_cc(ce->op), al});
        instrs.push_back(x86::Movzbq{al, dst});
    } else if (const auto *ne = std::get_if<cir::CNotExpr>(&expr)) {
        instrs.push_back(x86::Movq{atom_to_arg(ne->operand), dst});
        instrs.push_back(x86::Xorq{x86::Imm{1}, dst});
    }
}

/// @brief Emit instructions for a Tail
void emit_tail(const cir::Tail &tail, std::vector<x86::Instr> &instrs) {
    if (const auto *r = std::get_if<cir::Return>(&tail)) {
        emit_cexpr(r->expr, x86::RegArg{x86::Reg::Rax}, instrs);
        instrs.push_back(x86::Jmp{"conclusion"});
    } else if (const auto *g = std::get_if<cir::Goto>(&tail)) {
        instrs.push_back(x86::Jmp{g->label});
    } else if (const auto *is = std::get_if<cir::IfStmt>(&tail)) {
        instrs.push_back(x86::Cmpq{atom_to_arg(is->rhs),
                                     atom_to_arg(is->lhs)});
        instrs.push_back(x86::JmpIf{cmp_to_cc(is->op), is->then_label});
        instrs.push_back(x86::Jmp{is->else_label});
    }
}

x86::Block select_block(const cir::BasicBlock &blk) {
    std::vector<x86::Instr> instrs;
    for (size_t i = 0; i < blk.stmts.size(); ++i) {
        x86::Arg dst = x86::VarArg{blk.stmts[i].var};
        emit_cexpr(blk.stmts[i].expr, dst, instrs);
    }
    emit_tail(blk.tail, instrs);
    return x86::Block{std::move(instrs)};
}

} // namespace

x86::X86Program select_instructions(const cir::CProgram &prog) {
    x86::X86Program result;
    for (auto it = prog.blocks.begin(); it != prog.blocks.end(); ++it) {
        result.blocks[it->first] = select_block(it->second);
    }
    return result;
}
