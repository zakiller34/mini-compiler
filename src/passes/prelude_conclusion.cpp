#include "prelude_conclusion.h"

#include <vector>

namespace {

/// @brief Ordered list of callee-saved regs to push (deterministic order)
/// @ensures result contains only regs in used_callee_saved, in fixed order
std::vector<x86::Reg>
ordered_callee_saved(const std::set<x86::Reg> &used) {
    // Fixed order: rbx, r12, r13, r14
    static const std::vector<x86::Reg> order = {
        x86::Reg::Rbx, x86::Reg::R12, x86::Reg::R13, x86::Reg::R14};
    std::vector<x86::Reg> result;
    // invariant: result has matching regs from order[0..i)
    for (size_t i = 0; i < order.size(); ++i) {
        if (used.count(order[i]) > 0) {
            result.push_back(order[i]);
        }
    }
    return result;
}

} // namespace

/// @brief Generate main prelude and conclusion for x86 program
/// @requires prog has "start" block, stack_space is 16-aligned
/// @ensures result has "main" (frame setup) and "conclusion" (teardown) blocks
/// @ensures only used callee-saved regs are pushed/popped
x86::X86Program generate_prelude_conclusion(const x86::X86Program &prog) {
    x86::X86Program result = prog;
    auto callee_regs = ordered_callee_saved(prog.used_callee_saved);

    // main: push rbp, mov rsp->rbp, push callee-saved, sub stack_space, jmp
    x86::Block main_block;
    main_block.instrs.push_back(x86::Pushq{x86::RegArg{x86::Reg::Rbp}});
    main_block.instrs.push_back(
        x86::Movq{x86::RegArg{x86::Reg::Rsp}, x86::RegArg{x86::Reg::Rbp}});

    // Push callee-saved registers
    // invariant: callee_regs[0..i) pushed
    for (size_t i = 0; i < callee_regs.size(); ++i) {
        main_block.instrs.push_back(x86::Pushq{x86::RegArg{callee_regs[i]}});
    }

    if (prog.stack_space > 0) {
        main_block.instrs.push_back(
            x86::Subq{x86::Imm{prog.stack_space},
                       x86::RegArg{x86::Reg::Rsp}});
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

    return result;
}
