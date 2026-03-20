#pragma once

#include "../type.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace mc::x86 {

enum class Reg {
  Rsp, Rbp, Rax, Rbx, Rcx, Rdx, Rsi, Rdi,
  R8, R9, R10, R11, R12, R13, R14, R15
};

enum class CC { E, NE, L, LE, G, GE };

struct Imm { int64_t value; };
struct RegArg { Reg reg; };
struct Deref { Reg reg; int64_t offset; };
struct VarArg { std::string name; };
struct GlobalArg { std::string name; };

using Arg = std::variant<Imm, RegArg, Deref, VarArg, GlobalArg>;

// -- Instructions --

struct Addq { Arg src; Arg dst; };
struct Subq { Arg src; Arg dst; };
struct Movq { Arg src; Arg dst; };
struct Negq { Arg dst; };
struct Xorq { Arg src; Arg dst; };
struct Cmpq { Arg src; Arg dst; };
struct SetCC { CC cc; Arg dst; };
struct Movzbq { Arg src; Arg dst; };
struct Pushq { Arg src; };
struct Popq { Arg dst; };
struct Callq { std::string label; int64_t arity; };
struct Retq {};
struct Jmp { std::string label; };
struct JmpIf { CC cc; std::string label; };
struct Andq { Arg src; Arg dst; };
struct Sarq { Arg src; Arg dst; };
struct Leaq { Arg src; Arg dst; };

using Instr = std::variant<Addq, Subq, Movq, Negq, Xorq, Cmpq, SetCC,
                           Movzbq, Pushq, Popq, Callq, Retq, Jmp, JmpIf,
                           Andq, Sarq, Leaq>;

struct Block { std::vector<Instr> instrs; };

struct X86Program {
  std::map<std::string, Block> blocks;
  int64_t stack_space = 0;
  int64_t root_stack_space = 0;
  std::set<Reg> used_callee_saved;
  std::map<std::string, TypePtr> var_types;
  std::string dump() const;
};

std::string reg_name(Reg r);
std::string byte_reg_name(Reg r);
std::string cc_name(CC cc);
std::string dump_arg(const Arg &a);
std::string dump_instr(const Instr &i);

} // namespace mc::x86
