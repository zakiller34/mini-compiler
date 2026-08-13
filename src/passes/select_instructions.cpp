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

/// Mask that clears the 3 tag bits of a tagged pointer (…11111000)
constexpr int64_t kUntagPtrMask = -8;
/// Exit status for a trapped runtime type error (Siek 2023, section 1.5)
constexpr int64_t kTrappedErrorStatus = 255;
/// Mask isolating the length field of a heap tag (bits 1-6)
constexpr int64_t kLengthMask = 0x7E;

/// @brief Emit the C_Any tag operations (Siek 2023, section 9.8)
/// @requires dst is a var/register that may be clobbered
/// @ensures returns false if expr is not a C_Any expression
// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
bool emit_any_cexpr(const cir::CExpr &expr, const x86::Arg &dst,
                    std::vector<x86::Instr> &instrs) {
    x86::Arg r11 = x86::RegArg{x86::Reg::R11};
    x86::Arg rax = x86::RegArg{x86::Reg::Rax};
    if (const auto *ma = std::get_if<cir::CMakeAnyExpr>(&expr)) {
        // Pointers are already 8-byte aligned, so only scalars shift left
        instrs.push_back(x86::Movq{atom_to_arg(ma->value), dst});
        if (ma->tag == kTagInt || ma->tag == kTagBool || ma->tag == kTagVoid) {
            instrs.push_back(x86::Salq{x86::Imm{kTagShift}, dst});
        }
        instrs.push_back(x86::Orq{x86::Imm{ma->tag}, dst});
        return true;
    }
    if (const auto *ta = std::get_if<cir::CTagOfAnyExpr>(&expr)) {
        instrs.push_back(x86::Movq{atom_to_arg(ta->value), dst});
        instrs.push_back(x86::Andq{x86::Imm{kTagMask}, dst});
        return true;
    }
    if (const auto *vo = std::get_if<cir::CValueOfExpr>(&expr)) {
        if (is_root_type(vo->ftype) || is_fun_type(vo->ftype)) {
            instrs.push_back(x86::Movq{x86::Imm{kUntagPtrMask}, dst});
            instrs.push_back(x86::Andq{atom_to_arg(vo->value), dst});
        } else {
            instrs.push_back(x86::Movq{atom_to_arg(vo->value), dst});
            instrs.push_back(x86::Sarq{x86::Imm{kTagShift}, dst});
        }
        return true;
    }
    if (const auto *al = std::get_if<cir::CAnyVectorLengthExpr>(&expr)) {
        instrs.push_back(x86::Movq{x86::Imm{kUntagPtrMask}, r11});
        instrs.push_back(x86::Andq{atom_to_arg(al->vec), r11});
        instrs.push_back(x86::Movq{x86::Deref{x86::Reg::R11, 0}, r11});
        instrs.push_back(x86::Andq{x86::Imm{kLengthMask}, r11});
        instrs.push_back(x86::Sarq{x86::Imm{1}, r11});
        instrs.push_back(x86::Movq{r11, dst});
        return true;
    }
    if (const auto *ar = std::get_if<cir::CAnyVectorRefExpr>(&expr)) {
        // %r11 = untagged base + 8*(idx+1); the index is only known at runtime
        instrs.push_back(x86::Movq{x86::Imm{kUntagPtrMask}, r11});
        instrs.push_back(x86::Andq{atom_to_arg(ar->vec), r11});
        instrs.push_back(x86::Movq{atom_to_arg(ar->idx), rax});
        instrs.push_back(x86::Addq{x86::Imm{1}, rax});
        instrs.push_back(x86::Imulq{x86::Imm{kWordSize}, rax});
        instrs.push_back(x86::Addq{rax, r11});
        instrs.push_back(x86::Movq{x86::Deref{x86::Reg::R11, 0}, dst});
        return true;
    }
    if (const auto *as = std::get_if<cir::CAnyVectorSetExpr>(&expr)) {
        instrs.push_back(x86::Movq{x86::Imm{kUntagPtrMask}, r11});
        instrs.push_back(x86::Andq{atom_to_arg(as->vec), r11});
        instrs.push_back(x86::Movq{atom_to_arg(as->idx), rax});
        instrs.push_back(x86::Addq{x86::Imm{1}, rax});
        instrs.push_back(x86::Imulq{x86::Imm{kWordSize}, rax});
        instrs.push_back(x86::Addq{rax, r11});
        instrs.push_back(x86::Movq{atom_to_arg(as->val),
                                    x86::Deref{x86::Reg::R11, 0}});
        // any-vector-set! returns void
        instrs.push_back(x86::Movq{x86::Imm{0}, dst});
        return true;
    }
    return false;
}

/// @brief Emit instructions for CExpr, storing result in dst
// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
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
        // Set pointer mask bits 7+ for vector- or Any-typed elements
        for (int64_t i = 0; i < n; ++i) {
            if (i < static_cast<int64_t>(ae->type->elem_types.size()) &&
                is_root_type(ae->type->elem_types[static_cast<size_t>(i)])) {
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
    } else if (const auto *fr = std::get_if<cir::CFunRefExpr>(&expr)) {
        instrs.push_back(x86::Leaq{x86::GlobalArg{fr->name}, dst});
    } else if (const auto *ca = std::get_if<cir::CCallExpr>(&expr)) {
        static const x86::Reg arg_regs[] = {
            x86::Reg::Rdi, x86::Reg::Rsi, x86::Reg::Rdx,
            x86::Reg::Rcx, x86::Reg::R8, x86::Reg::R9};
        // invariant: args[0..i) moved to arg_regs[0..i)
        for (size_t i = 0; i < ca->args.size() && i < 6; ++i) {
            instrs.push_back(x86::Movq{atom_to_arg(ca->args[i]),
                x86::RegArg{arg_regs[i]}});
        }
        instrs.push_back(x86::IndirectCallq{atom_to_arg(ca->func),
            static_cast<int64_t>(ca->args.size())});
        instrs.push_back(x86::Movq{x86::RegArg{x86::Reg::Rax}, dst});
    } else if (const auto *ac = std::get_if<cir::CAllocateClosureExpr>(&expr)) {
        // Like CAllocate, plus arity encoded in tag bits 57-61.
        x86::Arg r11 = x86::RegArg{x86::Reg::R11};
        x86::Arg fp = x86::GlobalArg{"free_ptr"};
        int64_t n = ac->len;
        int64_t bytes = kWordSize * (n + 1);
        int64_t tag = (n & 0x3F) << 1;
        // invariant: tag has pointer-mask bits for slots[0..i)
        for (int64_t i = 0; i < n; ++i) {
            if (i < static_cast<int64_t>(ac->type->elem_types.size()) &&
                is_root_type(ac->type->elem_types[static_cast<size_t>(i)])) {
                tag |= (1LL << (7 + i));
            }
        }
        tag |= (ac->arity & 0x1F) << 57; // arity in bits 57-61
        instrs.push_back(x86::Movq{fp, r11});
        instrs.push_back(x86::Addq{x86::Imm{bytes}, fp});
        instrs.push_back(x86::Movq{x86::Imm{tag}, x86::Deref{x86::Reg::R11, 0}});
        instrs.push_back(x86::Movq{r11, dst});
    } else if (const auto *pa = std::get_if<cir::CProcArityExpr>(&expr)) {
        // movq clos -> r11, movq tag -> dst, sarq $57, andq $0x1F
        x86::Arg r11 = x86::RegArg{x86::Reg::R11};
        instrs.push_back(x86::Movq{atom_to_arg(pa->clos), r11});
        instrs.push_back(x86::Movq{x86::Deref{x86::Reg::R11, 0}, dst});
        instrs.push_back(x86::Sarq{x86::Imm{57}, dst});
        instrs.push_back(x86::Andq{x86::Imm{0x1F}, dst});
    } else {
        emit_any_cexpr(expr, dst, instrs);
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
    } else if (const auto *tc = std::get_if<cir::TailCall>(&tail)) {
        static const x86::Reg arg_regs[] = {
            x86::Reg::Rdi, x86::Reg::Rsi, x86::Reg::Rdx,
            x86::Reg::Rcx, x86::Reg::R8, x86::Reg::R9};
        // invariant: args[0..i) moved to arg_regs[0..i)
        for (size_t i = 0; i < tc->args.size() && i < 6; ++i) {
            instrs.push_back(x86::Movq{atom_to_arg(tc->args[i]),
                x86::RegArg{arg_regs[i]}});
        }
        instrs.push_back(x86::TailJmp{atom_to_arg(tc->func),
            static_cast<int64_t>(tc->args.size())});
    } else if (std::holds_alternative<cir::Exit>(tail)) {
        // trapped-error: halt with status 255 and never return
        instrs.push_back(x86::Movq{x86::Imm{kTrappedErrorStatus},
                                     x86::RegArg{x86::Reg::Rdi}});
        instrs.push_back(x86::Callq{"trapped_error", 1});
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

/// @brief Check if a block map has GC operations
static bool blocks_have_gc(
    const std::map<std::string, cir::BasicBlock> &blocks) {
    for (const auto &[label, blk] : blocks) {
        for (const auto &stmt : blk.stmts) {
            if (std::holds_alternative<cir::CAllocateExpr>(stmt.expr) ||
                std::holds_alternative<cir::CAllocateClosureExpr>(stmt.expr) ||
                std::holds_alternative<cir::CCollectExpr>(stmt.expr) ||
                std::holds_alternative<cir::CGlobalValueExpr>(stmt.expr)) {
                return true;
            }
        }
    }
    return false;
}

/// @brief Check if CProgram has any tuple/GC operations
static bool has_gc_ops(const cir::CProgram &prog) {
    if (blocks_have_gc(prog.blocks)) return true;
    for (const auto &def : prog.defs) {
        if (blocks_have_gc(def.blocks)) return true;
    }
    return false;
}

// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
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

    // Process each function def
    // invariant: result.defs has x86 defs for prog.defs[0..i)
    for (const auto &cdef : prog.defs) {
        x86::X86FunctionDef xdef;
        xdef.name = cdef.name;
        xdef.var_types = cdef.var_types;
        for (const auto &[label, blk] : cdef.blocks) {
            xdef.blocks[label] = select_block(blk);
        }
        // Move args from arg regs to param vars in start block
        auto &start_blk = xdef.blocks["start"];
        static const x86::Reg arg_regs[] = {
            x86::Reg::Rdi, x86::Reg::Rsi, x86::Reg::Rdx,
            x86::Reg::Rcx, x86::Reg::R8, x86::Reg::R9};
        std::vector<x86::Instr> param_instrs;
        // invariant: param_instrs has movs for params[0..i)
        for (size_t i = 0; i < cdef.params.size() && i < 6; ++i) {
            param_instrs.push_back(x86::Movq{
                x86::RegArg{arg_regs[i]},
                x86::VarArg{cdef.params[i]}});
        }
        param_instrs.insert(param_instrs.end(),
            start_blk.instrs.begin(), start_blk.instrs.end());
        start_blk.instrs = std::move(param_instrs);

        result.defs.push_back(std::move(xdef));
    }
    return result;
}

} // namespace mc
