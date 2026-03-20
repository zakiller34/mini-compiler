#include "select_instructions.h"

#include <vector>

namespace mc {

namespace {

/// @brief Word size in bytes for heap object layout
constexpr int64_t kWordSize = 8;

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
    } else if (const auto *ae = std::get_if<cir::CAllocateExpr>(&expr)) {
        // Bump free_ptr: movq free_ptr(%rip) -> r11, addq 8*(len+1) -> free_ptr
        // Store tag at 0(%r11), movq %r11 -> dst
        x86::Arg r11 = x86::RegArg{x86::Reg::R11};
        x86::Arg fp = x86::GlobalArg{"free_ptr"};
        int64_t n = ae->len;
        int64_t bytes = kWordSize * (n + 1);
        // Compute GC tag: bit0=0, bits1-6=length, bits7+=pointer mask
        int64_t tag = (n & 0x3F) << 1; // length in bits 1-6
        // Set pointer mask bits 7+ for vector-typed elements
        for (int64_t i = 0; i < n; ++i) {
            if (i < static_cast<int64_t>(ae->type->elem_types.size()) &&
                is_vector_type(ae->type->elem_types[static_cast<size_t>(i)])) {
                tag |= (1LL << (7 + i));
            }
        }
        instrs.push_back(x86::Movq{fp, r11});
        instrs.push_back(x86::Addq{x86::Imm{bytes}, fp});
        instrs.push_back(x86::Movq{x86::Imm{tag}, x86::Deref{x86::Reg::R11, 0}});
        instrs.push_back(x86::Movq{r11, dst});
    } else if (const auto *vr = std::get_if<cir::CVectorRefExpr>(&expr)) {
        // movq vec -> r11, movq 8*(i+1)(%r11) -> dst
        x86::Arg r11 = x86::RegArg{x86::Reg::R11};
        instrs.push_back(x86::Movq{atom_to_arg(vr->vec), r11});
        instrs.push_back(x86::Movq{
            x86::Deref{x86::Reg::R11, kWordSize * (vr->index + 1)}, dst});
    } else if (const auto *vs = std::get_if<cir::CVectorSetExpr>(&expr)) {
        // movq vec -> r11, movq val -> 8*(i+1)(%r11)
        x86::Arg r11 = x86::RegArg{x86::Reg::R11};
        instrs.push_back(x86::Movq{atom_to_arg(vs->vec), r11});
        instrs.push_back(x86::Movq{atom_to_arg(vs->val),
            x86::Deref{x86::Reg::R11, kWordSize * (vs->index + 1)}});
        // vector-set! returns void, store 0 in dst
        instrs.push_back(x86::Movq{x86::Imm{0}, dst});
    } else if (const auto *vl = std::get_if<cir::CVectorLengthExpr>(&expr)) {
        // movq vec -> r11, movq 0(%r11) -> dst (tag)
        // andq $0x7E -> dst, sarq $1 -> dst
        x86::Arg r11 = x86::RegArg{x86::Reg::R11};
        instrs.push_back(x86::Movq{atom_to_arg(vl->vec), r11});
        instrs.push_back(x86::Movq{x86::Deref{x86::Reg::R11, 0}, dst});
        instrs.push_back(x86::Andq{x86::Imm{0x7E}, dst});
        instrs.push_back(x86::Sarq{x86::Imm{1}, dst});
    } else if (const auto *gv = std::get_if<cir::CGlobalValueExpr>(&expr)) {
        instrs.push_back(x86::Movq{x86::GlobalArg{gv->name}, dst});
    } else if (const auto *ce = std::get_if<cir::CCollectExpr>(&expr)) {
        // movq %r15 -> %rdi, movq $bytes -> %rsi, callq collect
        instrs.push_back(x86::Movq{x86::RegArg{x86::Reg::R15},
                                     x86::RegArg{x86::Reg::Rdi}});
        instrs.push_back(x86::Movq{x86::Imm{ce->bytes},
                                     x86::RegArg{x86::Reg::Rsi}});
        instrs.push_back(x86::Callq{"collect", 2});
        instrs.push_back(x86::Movq{x86::Imm{0}, dst});
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
    for (const auto &stmt : blk.stmts) {
        x86::Arg dst = x86::VarArg{stmt.var};
        emit_cexpr(stmt.expr, dst, instrs);
    }
    emit_tail(blk.tail, instrs);
    return x86::Block{std::move(instrs)};
}

} // namespace

/// @brief Check if CProgram has any tuple/GC operations
static bool has_gc_ops(const cir::CProgram &prog) {
    for (const auto &[label, blk] : prog.blocks) {
        for (const auto &stmt : blk.stmts) {
            if (std::holds_alternative<cir::CAllocateExpr>(stmt.expr) ||
                std::holds_alternative<cir::CCollectExpr>(stmt.expr) ||
                std::holds_alternative<cir::CGlobalValueExpr>(stmt.expr)) {
                return true;
            }
        }
    }
    return false;
}

x86::X86Program select_instructions(const cir::CProgram &prog) {
    x86::X86Program result;
    result.var_types = prog.var_types;
    // If program has GC operations, set a minimal root_stack_space
    // to trigger GC initialization in prelude
    if (has_gc_ops(prog)) {
        result.root_stack_space = 8; // at least one slot
    }
    for (const auto &[label, blk] : prog.blocks) {
        result.blocks[label] = select_block(blk);
    }
    return result;
}

} // namespace mc
