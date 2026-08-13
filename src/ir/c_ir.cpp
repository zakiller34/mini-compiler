#include "c_ir.h"

#include <string>

namespace mc::cir {

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

/// @brief Dump the C_Any expressions (figure 9.11)
/// @ensures "?" if e is not one of them
static std::string dump_any_cexpr(const CExpr &e) {
    if (const auto *ma = std::get_if<CMakeAnyExpr>(&e)) {
        return "(make-any " + dump_atom(ma->value) + " " +
               std::to_string(ma->tag) + ")";
    }
    if (const auto *ta = std::get_if<CTagOfAnyExpr>(&e)) {
        return "(tag-of-any " + dump_atom(ta->value) + ")";
    }
    if (const auto *vo = std::get_if<CValueOfExpr>(&e)) {
        return "(value-of " + dump_atom(vo->value) + " " +
               vo->ftype->dump() + ")";
    }
    if (const auto *ar = std::get_if<CAnyVectorRefExpr>(&e)) {
        return "(any-vector-ref " + dump_atom(ar->vec) + " " +
               dump_atom(ar->idx) + ")";
    }
    if (const auto *as = std::get_if<CAnyVectorSetExpr>(&e)) {
        return "(any-vector-set! " + dump_atom(as->vec) + " " +
               dump_atom(as->idx) + " " + dump_atom(as->val) + ")";
    }
    if (const auto *al = std::get_if<CAnyVectorLengthExpr>(&e)) {
        return "(any-vector-length " + dump_atom(al->vec) + ")";
    }
    return "?";
}

// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
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
    if (const auto *ae = std::get_if<CAllocateExpr>(&e)) {
        return "(allocate " + std::to_string(ae->len) + " " +
               ae->type->dump() + ")";
    }
    if (const auto *vr = std::get_if<CVectorRefExpr>(&e)) {
        return "(vector-ref " + dump_atom(vr->vec) + " " +
               std::to_string(vr->index) + ")";
    }
    if (const auto *vs = std::get_if<CVectorSetExpr>(&e)) {
        return "(vector-set! " + dump_atom(vs->vec) + " " +
               std::to_string(vs->index) + " " + dump_atom(vs->val) + ")";
    }
    if (const auto *vl = std::get_if<CVectorLengthExpr>(&e)) {
        return "(vector-length " + dump_atom(vl->vec) + ")";
    }
    if (const auto *gv = std::get_if<CGlobalValueExpr>(&e)) {
        return "(global-value " + gv->name + ")";
    }
    if (const auto *ce = std::get_if<CCollectExpr>(&e)) {
        return "(collect " + std::to_string(ce->bytes) + ")";
    }
    if (const auto *fr = std::get_if<CFunRefExpr>(&e)) {
        return "(fun-ref " + fr->name + " " + std::to_string(fr->arity) + ")";
    }
    if (const auto *ca = std::get_if<CCallExpr>(&e)) {
        std::string r = "(call " + dump_atom(ca->func);
        for (const auto &a : ca->args) r += " " + dump_atom(a);
        return r + ")";
    }
    if (const auto *ac = std::get_if<CAllocateClosureExpr>(&e)) {
        return "(allocate-closure " + std::to_string(ac->len) + " " +
               ac->type->dump() + " " + std::to_string(ac->arity) + ")";
    }
    if (const auto *pa = std::get_if<CProcArityExpr>(&e)) {
        return "(procedure-arity " + dump_atom(pa->clos) + ")";
    }
    return dump_any_cexpr(e);
}

std::string dump_tail(const Tail &t) {
    if (const auto *r = std::get_if<Return>(&t)) {
        return "(return " + dump_cexpr(r->expr) + ")";
    }
    if (const auto *g = std::get_if<Goto>(&t)) {
        return "(goto " + g->label + ")";
    }
    if (const auto *tc = std::get_if<TailCall>(&t)) {
        std::string r = "(tail-call " + dump_atom(tc->func);
        for (const auto &a : tc->args) r += " " + dump_atom(a);
        return r + ")";
    }
    if (std::holds_alternative<Exit>(t)) {
        return "(exit)";
    }
    const auto &is = std::get<IfStmt>(t);
    return std::string("(if (") + cmp_op_name(is.op) + " " +
           dump_atom(is.lhs) + " " + dump_atom(is.rhs) + ") (goto " +
           is.then_label + ") (goto " + is.else_label + "))";
}

std::string CProgram::dump() const {
    std::string result = "(c-program\n";
    for (const auto &[label, blk] : blocks) {
        result += "  (" + label + "\n";
        for (const auto &stmt : blk.stmts) {
            result += "    (assign " + stmt.var + " " +
                      dump_cexpr(stmt.expr) + ")\n";
        }
        result += "    " + dump_tail(blk.tail) + ")\n";
    }
    result += ")";
    return result;
}

} // namespace mc::cir
