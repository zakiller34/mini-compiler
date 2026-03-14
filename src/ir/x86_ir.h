#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace x86 {

enum class Reg {
  Rsp,
  Rbp,
  Rax,
  Rbx,
  Rcx,
  Rdx,
  Rsi,
  Rdi,
  R8,
  R9,
  R10,
  R11,
  R12,
  R13,
  R14,
  R15
};

struct Imm {
  int64_t value;
};

struct RegArg {
  Reg reg;
};

struct Deref {
  Reg reg;
  int64_t offset;
};

struct VarArg {
  std::string name;
};

using Arg = std::variant<Imm, RegArg, Deref, VarArg>;

// -- Instructions --

struct Addq {
  Arg src;
  Arg dst;
};
struct Subq {
  Arg src;
  Arg dst;
};
struct Movq {
  Arg src;
  Arg dst;
};
struct Negq {
  Arg dst;
};
struct Pushq {
  Arg src;
};
struct Popq {
  Arg dst;
};
struct Callq {
  std::string label;
  int64_t arity;
};
struct Retq {};
struct Jmp {
  std::string label;
};

using Instr =
    std::variant<Addq, Subq, Movq, Negq, Pushq, Popq, Callq, Retq, Jmp>;

struct Block {
  std::vector<Instr> instrs;
};

struct X86Program {
  std::map<std::string, Block> blocks;
  int64_t stack_space = 0;
  std::string dump() const;
};

std::string reg_name(Reg r);
std::string dump_arg(const Arg &a);
std::string dump_instr(const Instr &i);

} // namespace x86
