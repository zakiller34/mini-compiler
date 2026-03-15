#include "c_ir.h"

#include <string>

namespace cir {

std::string dump_atom(const Atom &a) {
    if (const auto *i = std::get_if<IntAtom>(&a)) {
        return std::to_string(i->value);
    }
    if (const auto *b = std::get_if<BoolAtom>(&a)) {
        return b->value ? "true" : "false";
    }
    return std::get<VarAtom>(a).name;
}

static const char *cmp_op_name(CCmpOp op) {
    switch (op) {
    case CCmpOp::Eq: return "==";
    case CCmpOp::Lt: return "<";
    case CCmpOp::Le: return "<=";
    case CCmpOp::Gt: return ">";
    case CCmpOp::Ge: return ">=";
    }
    return "?";
}

std::string dump_cexpr(const CExpr &e) {
    if (const auto *ae = std::get_if<AtomExpr>(&e)) {
        return dump_atom(ae->atom);
    }
    if (std::holds_alternative<CReadExpr>(e)) {
        return "(read)";
    }
    if (const auto *ue = std::get_if<CUnaryExpr>(&e)) {
        const char *op = (ue->op == CUnaryOp::Neg) ? "-" : "not";
        return std::string("(") + op + " " + dump_atom(ue->operand) + ")";
    }
    if (const auto *be = std::get_if<CBinaryExpr>(&e)) {
        const char *op = (be->op == CBinaryOp::Add) ? "+" : "-";
        return std::string("(") + op + " " + dump_atom(be->lhs) + " " +
               dump_atom(be->rhs) + ")";
    }
    if (const auto *ce = std::get_if<CCmpExpr>(&e)) {
        return std::string("(") + cmp_op_name(ce->op) + " " +
               dump_atom(ce->lhs) + " " + dump_atom(ce->rhs) + ")";
    }
    if (const auto *ne = std::get_if<CNotExpr>(&e)) {
        return "(not " + dump_atom(ne->operand) + ")";
    }
    return "?";
}

std::string dump_tail(const Tail &t) {
    if (const auto *r = std::get_if<Return>(&t)) {
        return "(return " + dump_cexpr(r->expr) + ")";
    }
    if (const auto *g = std::get_if<Goto>(&t)) {
        return "(goto " + g->label + ")";
    }
    const auto &is = std::get<IfStmt>(t);
    return std::string("(if (") + cmp_op_name(is.op) + " " +
           dump_atom(is.lhs) + " " + dump_atom(is.rhs) + ") (goto " +
           is.then_label + ") (goto " + is.else_label + "))";
}

std::string CProgram::dump() const {
    std::string result = "(c-program\n";
    for (auto it = blocks.begin(); it != blocks.end(); ++it) {
        result += "  (" + it->first + "\n";
        const auto &blk = it->second;
        for (size_t i = 0; i < blk.stmts.size(); ++i) {
            result += "    (assign " + blk.stmts[i].var + " " +
                      dump_cexpr(blk.stmts[i].expr) + ")\n";
        }
        result += "    " + dump_tail(blk.tail) + ")\n";
    }
    result += ")";
    return result;
}

} // namespace cir
