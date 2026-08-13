#include "prelude_conclusion.h"

#include <vector>

namespace mc {

namespace {

/// @brief Initial heap size in bytes (16 KiB)
constexpr int64_t kHeapSize = 16384;

/// @brief Initial root stack size in bytes (64 KiB)
constexpr int64_t kRootstackSize = 65536;

/// @brief Word size in bytes for root stack slot layout
constexpr int64_t kWordSize = 8;

/// @brief Ordered list of callee-saved regs to push (deterministic order)
/// @ensures result contains only regs in used_callee_saved, in fixed order
std::vector<x86::Reg>
ordered_callee_saved(const std::set<x86::Reg> &used) {
    // Fixed order: rbx, r12, r13, r14
    static const std::vector<x86::Reg> order = {
        x86::Reg::Rbx, x86::Reg::R12, x86::Reg::R13, x86::Reg::R14};
    std::vector<x86::Reg> result;
    // invariant: result has matching regs from order[0..i)
    for (const auto &reg : order) {
        if (used.count(reg) > 0) {
            result.push_back(reg);
        }
    }
    return result;
}

/// @brief Check if program uses any tuple/GC features
bool needs_gc(const x86::X86Program &prog) {
    return prog.root_stack_space > 0 || !prog.var_types.empty();
}

} // namespace

/// @brief Generate main prelude and conclusion for x86 program
/// @requires prog has "start" block, stack_space is 16-aligned
/// @ensures result has "main" (frame setup) and "conclusion" (teardown) blocks
/// @ensures only used callee-saved regs are pushed/popped
// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
x86::X86Program generate_prelude_conclusion(const x86::X86Program &prog) {
    x86::X86Program result = prog;
    auto callee_regs = ordered_callee_saved(prog.used_callee_saved);
    bool gc = needs_gc(prog);

    // main: push rbp, mov rsp->rbp, push callee-saved, sub stack_space, jmp
    x86::Block main_block;
    main_block.instrs.push_back(x86::Pushq{x86::RegArg{x86::Reg::Rbp}});
    main_block.instrs.push_back(
        x86::Movq{x86::RegArg{x86::Reg::Rsp}, x86::RegArg{x86::Reg::Rbp}});

    // Push callee-saved registers
    // invariant: callee_regs[0..i) pushed
    for (const auto &reg : callee_regs) {
        main_block.instrs.push_back(x86::Pushq{x86::RegArg{reg}});
    }

    if (prog.stack_space > 0) {
        main_block.instrs.push_back(
            x86::Subq{x86::Imm{prog.stack_space},
                       x86::RegArg{x86::Reg::Rsp}});
    }

    if (gc) {
        // Initialize GC: movq $kHeapSize, %rdi; movq $kRootstackSize, %rsi; callq initialize
        main_block.instrs.push_back(
            x86::Movq{x86::Imm{kHeapSize}, x86::RegArg{x86::Reg::Rdi}});
        main_block.instrs.push_back(
            x86::Movq{x86::Imm{kRootstackSize}, x86::RegArg{x86::Reg::Rsi}});
        main_block.instrs.push_back(x86::Callq{"initialize", 2});

        // Setup root stack pointer: movq rootstack_begin(%rip), %r15
        main_block.instrs.push_back(
            x86::Movq{x86::GlobalArg{"rootstack_begin"},
                       x86::RegArg{x86::Reg::R15}});

        // Zero root stack slots
        int64_t num_slots = prog.root_stack_space / kWordSize;
        // invariant: slots [0..i) zeroed
        for (int64_t i = 0; i < num_slots; ++i) {
            main_block.instrs.push_back(
                x86::Movq{x86::Imm{0},
                           x86::Deref{x86::Reg::R15, kWordSize * i}});
        }
    }

    main_block.instrs.push_back(x86::Jmp{"start"});
    result.blocks["main"] = std::move(main_block);

    // conclusion: add stack_space, pop callee-saved (reverse), pop rbp, ret
    x86::Block conclusion;
    if (prog.stack_space > 0) {
        conclusion.instrs.push_back(
            x86::Addq{x86::Imm{prog.stack_space},
                       x86::RegArg{x86::Reg::Rsp}});
    }

    // Pop callee-saved in reverse order
    // invariant: callee_regs[i+1..n) popped
    for (auto i = static_cast<int64_t>(callee_regs.size()) - 1; i >= 0; --i) {
        conclusion.instrs.push_back(
            x86::Popq{x86::RegArg{callee_regs[static_cast<size_t>(i)]}});
    }

    conclusion.instrs.push_back(x86::Popq{x86::RegArg{x86::Reg::Rbp}});
    conclusion.instrs.push_back(x86::Retq{});
    result.blocks["conclusion"] = std::move(conclusion);

    // Process each function def (clear copied defs first)
    result.defs.clear();
    // invariant: result.defs has prologue/epilogue for prog.defs[0..i)
    for (const auto &xdef : prog.defs) {
        x86::X86FunctionDef new_def = xdef;
        auto fn_callee = ordered_callee_saved(xdef.used_callee_saved);

        // Function prologue
        x86::Block func_entry;
        func_entry.instrs.push_back(
            x86::Pushq{x86::RegArg{x86::Reg::Rbp}});
        func_entry.instrs.push_back(
            x86::Movq{x86::RegArg{x86::Reg::Rsp},
                       x86::RegArg{x86::Reg::Rbp}});
        // invariant: fn_callee[0..i) pushed
        for (const auto &reg : fn_callee) {
            func_entry.instrs.push_back(x86::Pushq{x86::RegArg{reg}});
        }
        if (xdef.stack_space > 0) {
            func_entry.instrs.push_back(
                x86::Subq{x86::Imm{xdef.stack_space},
                           x86::RegArg{x86::Reg::Rsp}});
        }
        func_entry.instrs.push_back(
            x86::Jmp{xdef.name + "_start"});
        new_def.blocks[xdef.name] = std::move(func_entry);

        // Rename ALL blocks to "funcname_label" to avoid collision
        // Collect original labels first, then rename
        std::vector<std::string> orig_labels;
        for (const auto &[label, blk] : new_def.blocks) {
            if (label != xdef.name) { // skip entry we just created
                orig_labels.push_back(label);
            }
        }
        // invariant: orig_labels[0..i) renamed
        for (const auto &old_label : orig_labels) {
            std::string new_label = xdef.name + "_" + old_label;
            new_def.blocks[new_label] =
                std::move(new_def.blocks[old_label]);
            new_def.blocks.erase(old_label);
        }

        // Insert epilogue before TailJmp instructions
        for (auto &[lbl, blk] : new_def.blocks) {
            std::vector<x86::Instr> new_instrs;
            for (auto &instr : blk.instrs) {
                if (const auto *tj = std::get_if<x86::TailJmp>(&instr)) {
                    // The target may live in a callee-saved register that the
                    // epilogue restores; stash it in %rax (reserved, not
                    // popped, and overwritten by the callee anyway) first.
                    new_instrs.push_back(
                        x86::Movq{tj->func, x86::RegArg{x86::Reg::Rax}});
                    if (xdef.stack_space > 0) {
                        new_instrs.push_back(x86::Addq{
                            x86::Imm{xdef.stack_space},
                            x86::RegArg{x86::Reg::Rsp}});
                    }
                    for (auto ci = static_cast<int64_t>(fn_callee.size()) - 1;
                         ci >= 0; --ci) {
                        new_instrs.push_back(x86::Popq{x86::RegArg{
                            fn_callee[static_cast<size_t>(ci)]}});
                    }
                    new_instrs.push_back(x86::Popq{
                        x86::RegArg{x86::Reg::Rbp}});
                    new_instrs.push_back(x86::TailJmp{
                        x86::RegArg{x86::Reg::Rax}, tj->arity});
                    continue;
                }
                new_instrs.push_back(std::move(instr));
            }
            blk.instrs = std::move(new_instrs);
        }

        // Build set of old labels for fast lookup (includes "conclusion")
        std::set<std::string> internal_labels(orig_labels.begin(),
                                               orig_labels.end());
        internal_labels.insert("conclusion");

        // Rename all label references in jumps
        // invariant: all blocks have jump refs updated
        for (auto &[label, blk] : new_def.blocks) {
            for (auto &instr : blk.instrs) {
                if (auto *j = std::get_if<x86::Jmp>(&instr)) {
                    if (internal_labels.count(j->label) > 0) {
                        j->label = xdef.name + "_" + j->label;
                    }
                } else if (auto *jc = std::get_if<x86::JmpIf>(&instr)) {
                    if (internal_labels.count(jc->label) > 0) {
                        jc->label = xdef.name + "_" + jc->label;
                    }
                }
            }
        }

        // Function conclusion
        x86::Block func_conclusion;
        if (xdef.stack_space > 0) {
            func_conclusion.instrs.push_back(
                x86::Addq{x86::Imm{xdef.stack_space},
                           x86::RegArg{x86::Reg::Rsp}});
        }
        // invariant: fn_callee[i+1..n) popped
        for (auto i = static_cast<int64_t>(fn_callee.size()) - 1;
             i >= 0; --i) {
            func_conclusion.instrs.push_back(
                x86::Popq{x86::RegArg{
                    fn_callee[static_cast<size_t>(i)]}});
        }
        func_conclusion.instrs.push_back(
            x86::Popq{x86::RegArg{x86::Reg::Rbp}});
        func_conclusion.instrs.push_back(x86::Retq{});
        new_def.blocks[xdef.name + "_conclusion"] =
            std::move(func_conclusion);

        result.defs.push_back(std::move(new_def));
    }

    return result;
}

} // namespace mc
