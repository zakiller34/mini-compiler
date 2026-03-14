#include "c_ir.h"

#include <string>

namespace cir {

/// @brief Dump an Atom as S-expr
/// @ensures result is "(int N)" or "(var name)"
std::string dump_atom(const Atom &a) {
    if (const auto *i = std::get_if<IntAtom>(&a)) {
        return std::to_string(i->value);
    }
    return std::get<VarAtom>(a).name;
}

/// @brief Dump a CExpr as S-expr
/// @ensures result is valid S-expr string
std::string dump_cexpr(const CExpr &e) {
    if (const auto *ae = std::get_if<AtomExpr>(&e)) {
        return dump_atom(ae->atom);
    }
    if (std::holds_alternative<CReadExpr>(e)) {
        return "(read)";
    }
    if (const auto *ue = std::get_if<CUnaryExpr>(&e)) {
        return "(- " + dump_atom(ue->operand) + ")";
    }
    const auto &be = std::get<CBinaryExpr>(e);
    std::string op = (be.op == CBinaryOp::Add) ? "+" : "-";
    return "(" + op + " " + dump_atom(be.lhs) + " " + dump_atom(be.rhs) + ")";
}

/// @brief Dump entire CProgram as S-expr
/// @ensures result shows all blocks with assignments and returns
std::string CProgram::dump() const {
    std::string result = "(c-program\n";

    // invariant: result accumulates all processed blocks
    // decreases: blocks.end() - it
    for (auto it = blocks.begin(); it != blocks.end(); ++it) {
        result += "  (" + it->first + "\n";
        const auto &blk = it->second;

        // invariant: all stmts[0..i) are dumped
        // decreases: stmts.size() - i
        for (size_t i = 0; i < blk.stmts.size(); ++i) {
            result +=
                "    (assign " + blk.stmts[i].var + " " + dump_cexpr(blk.stmts[i].expr) + ")\n";
        }
        result += "    (return " + dump_cexpr(blk.ret) + "))\n";
    }
    result += ")";
    return result;
}

} // namespace cir
