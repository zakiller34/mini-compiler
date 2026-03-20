#include "patch_instructions.h"

#include <vector>

namespace {

bool is_mem(const x86::Arg &a) {
    return std::holds_alternative<x86::Deref>(a) ||
           std::holds_alternative<x86::GlobalArg>(a);
}

bool args_equal(const x86::Arg &a, const x86::Arg &b) {
    if (const auto *da = std::get_if<x86::Deref>(&a)) {
        if (const auto *db = std::get_if<x86::Deref>(&b))
            return da->reg == db->reg && da->offset == db->offset;
    }
    if (const auto *ra = std::get_if<x86::RegArg>(&a)) {
        if (const auto *rb = std::get_if<x86::RegArg>(&b))
            return ra->reg == rb->reg;
    }
    if (const auto *ia = std::get_if<x86::Imm>(&a)) {
        if (const auto *ib = std::get_if<x86::Imm>(&b))
            return ia->value == ib->value;
    }
    return false;
}

/// @brief Fix two-memory-operand for a src,dst instruction pair
template <typename F>
void patch_two_arg(const x86::Arg &src, const x86::Arg &dst,
                   F make_instr, std::vector<x86::Instr> &out) {
    const x86::Arg rax = x86::RegArg{x86::Reg::Rax};
    if (is_mem(src) && is_mem(dst)) {
        out.push_back(x86::Movq{src, rax});
        out.push_back(make_instr(rax, dst));
    } else {
        out.push_back(make_instr(src, dst));
    }
}

void patch_one(const x86::Instr &instr, std::vector<x86::Instr> &out) {
    const x86::Arg rax = x86::RegArg{x86::Reg::Rax};

    if (const auto *m = std::get_if<x86::Movq>(&instr)) {
        if (args_equal(m->src, m->dst)) return;
        if (is_mem(m->src) && is_mem(m->dst)) {
            out.push_back(x86::Movq{m->src, rax});
            out.push_back(x86::Movq{rax, m->dst});
            return;
        }
        out.push_back(instr);
    } else if (const auto *a = std::get_if<x86::Addq>(&instr)) {
        patch_two_arg(a->src, a->dst,
            [](auto s, auto d) { return x86::Instr{x86::Addq{s, d}}; }, out);
    } else if (const auto *s = std::get_if<x86::Subq>(&instr)) {
        patch_two_arg(s->src, s->dst,
            [](auto ss, auto d) { return x86::Instr{x86::Subq{ss, d}}; }, out);
    } else if (const auto *x = std::get_if<x86::Xorq>(&instr)) {
        patch_two_arg(x->src, x->dst,
            [](auto ss, auto d) { return x86::Instr{x86::Xorq{ss, d}}; }, out);
    } else if (const auto *c = std::get_if<x86::Cmpq>(&instr)) {
        // cmpq: dst can't be immediate; can't have two memory operands
        bool dst_imm = std::holds_alternative<x86::Imm>(c->dst);
        if (dst_imm || (is_mem(c->src) && is_mem(c->dst))) {
            out.push_back(x86::Movq{c->dst, rax});
            out.push_back(x86::Cmpq{c->src, rax});
        } else {
            out.push_back(instr);
        }
    } else if (const auto *mz = std::get_if<x86::Movzbq>(&instr)) {
        if (is_mem(mz->src) && is_mem(mz->dst)) {
            out.push_back(x86::Movzbq{mz->src, rax});
            out.push_back(x86::Movq{rax, mz->dst});
        } else {
            out.push_back(instr);
        }
    } else if (const auto *aq = std::get_if<x86::Andq>(&instr)) {
        patch_two_arg(aq->src, aq->dst,
            [](auto s, auto d) { return x86::Instr{x86::Andq{s, d}}; }, out);
    } else if (const auto *sq = std::get_if<x86::Sarq>(&instr)) {
        // sarq only needs src as imm or %cl, dst can be mem
        out.push_back(instr);
    } else if (const auto *lq = std::get_if<x86::Leaq>(&instr)) {
        // leaq src, dst — dst must be reg
        out.push_back(instr);
    } else {
        out.push_back(instr);
    }
}

} // namespace

x86::X86Program patch_instructions(const x86::X86Program &prog) {
    x86::X86Program result;
    result.stack_space = prog.stack_space;
    result.root_stack_space = prog.root_stack_space;
    result.used_callee_saved = prog.used_callee_saved;
    result.var_types = prog.var_types;

    for (const auto &[label, blk] : prog.blocks) {
        std::vector<x86::Instr> patched;
        for (const auto &instr : blk.instrs) {
            patch_one(instr, patched);
        }
        result.blocks[label] = x86::Block{std::move(patched)};
    }
    return result;
}
