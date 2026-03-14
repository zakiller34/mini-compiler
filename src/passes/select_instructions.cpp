#include "select_instructions.h"

#include <vector>

namespace {

/// @brief Convert CIR Atom to x86 Arg
/// @ensures result is Imm or VarArg
x86::Arg atom_to_arg(const cir::Atom &a) {
    if (const auto *i = std::get_if<cir::IntAtom>(&a)) {
        return x86::Imm{i->value};
    }
    return x86::VarArg{std::get<cir::VarAtom>(a).name};
}

/// @brief Emit instructions for CExpr, storing result in dst
/// @requires dst is valid x86::Arg
/// @ensures instrs has instructions that compute expr into dst
void emit_cexpr(const cir::CExpr &expr, const x86::Arg &dst, std::vector<x86::Instr> &instrs) {
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
    }
}

/// @brief Select instructions for a single basic block
/// @requires blk has valid stmts and ret
/// @ensures result block has x86 instructions
x86::Block select_block(const cir::BasicBlock &blk) {
    std::vector<x86::Instr> instrs;

    // invariant: instrs has instructions for stmts[0..i)
    // decreases: blk.stmts.size() - i
    for (size_t i = 0; i < blk.stmts.size(); ++i) {
        x86::Arg dst = x86::VarArg{blk.stmts[i].var};
        emit_cexpr(blk.stmts[i].expr, dst, instrs);
    }

    // Return: move result to %rax, jump to conclusion
    emit_cexpr(blk.ret, x86::RegArg{x86::Reg::Rax}, instrs);
    instrs.push_back(x86::Jmp{"conclusion"});

    return x86::Block{std::move(instrs)};
}

} // namespace

/// @brief Select x86 instructions from C_Var IR
/// @requires prog has valid blocks
/// @ensures result has pseudo-x86 blocks
x86::X86Program select_instructions(const cir::CProgram &prog) {
    x86::X86Program result;

    // invariant: result.blocks has all processed CIR blocks
    // decreases: prog.blocks.end() - it
    for (auto it = prog.blocks.begin(); it != prog.blocks.end(); ++it) {
        result.blocks[it->first] = select_block(it->second);
    }
    return result;
}
