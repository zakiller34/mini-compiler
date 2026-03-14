#include "prelude_conclusion.h"

/// @brief Generate main prelude and conclusion for x86 program
/// @requires prog has "start" block, stack_space is 16-aligned
/// @ensures result has "main" (frame setup) and "conclusion" (teardown) blocks
x86::X86Program generate_prelude_conclusion(const x86::X86Program &prog) {
    x86::X86Program result = prog;

    // main: push rbp, mov rsp->rbp, sub stack_space, jmp start
    x86::Block main_block;
    main_block.instrs.push_back(x86::Pushq{x86::RegArg{x86::Reg::Rbp}});
    main_block.instrs.push_back(
        x86::Movq{x86::RegArg{x86::Reg::Rsp}, x86::RegArg{x86::Reg::Rbp}});
    if (prog.stack_space > 0) {
        main_block.instrs.push_back(
            x86::Subq{x86::Imm{prog.stack_space}, x86::RegArg{x86::Reg::Rsp}});
    }
    main_block.instrs.push_back(x86::Jmp{"start"});
    result.blocks["main"] = std::move(main_block);

    // conclusion: add stack_space, pop rbp, ret
    x86::Block conclusion;
    if (prog.stack_space > 0) {
        conclusion.instrs.push_back(
            x86::Addq{x86::Imm{prog.stack_space}, x86::RegArg{x86::Reg::Rsp}});
    }
    conclusion.instrs.push_back(x86::Popq{x86::RegArg{x86::Reg::Rbp}});
    conclusion.instrs.push_back(x86::Retq{});
    result.blocks["conclusion"] = std::move(conclusion);

    return result;
}
