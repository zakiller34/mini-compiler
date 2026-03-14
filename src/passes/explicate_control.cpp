#include "explicate_control.h"

#include <vector>

namespace {

/// @brief Convert leaf expr to Atom
/// @requires e is IntExpr or VarExpr
cir::Atom make_atom(const Expr *e) {
    if (const auto *ie = dynamic_cast<const IntExpr *>(e)) {
        return cir::IntAtom{ie->value};
    }
    return cir::VarAtom{dynamic_cast<const VarExpr *>(e)->name};
}

/// @brief Convert an AST Expr to a CExpr atom-level expression
/// @requires e is IntExpr, VarExpr, ReadExpr, UnaryExpr, or BinaryExpr (ANF)
/// @ensures result is a valid CExpr
cir::CExpr expr_to_cexpr(const Expr *e) {
    if (const auto *ie = dynamic_cast<const IntExpr *>(e)) {
        return cir::AtomExpr{cir::IntAtom{ie->value}};
    }
    if (const auto *ve = dynamic_cast<const VarExpr *>(e)) {
        return cir::AtomExpr{cir::VarAtom{ve->name}};
    }
    if (dynamic_cast<const ReadExpr *>(e) != nullptr) {
        return cir::CReadExpr{};
    }
    if (const auto *ue = dynamic_cast<const UnaryExpr *>(e)) {
        return cir::CUnaryExpr{cir::CUnaryOp::Neg, make_atom(ue->operand.get())};
    }
    const auto *be = dynamic_cast<const BinaryExpr *>(e);
    auto op = (be->op == BinaryOp::Add) ? cir::CBinaryOp::Add : cir::CBinaryOp::Sub;
    return cir::CBinaryExpr{op, make_atom(be->lhs.get()), make_atom(be->rhs.get())};
}

} // namespace

/// @brief Explicate control: flatten let-nesting into basic block
/// @requires prog.body != nullptr
/// @ensures result.blocks["start"] contains all assignments + return
cir::CProgram explicate_control(const Program &prog) {
    cir::BasicBlock block;

    const Expr *cur = prog.body.get();

    // decreases depth of nested lets
    // invariant block.stmts has all assignments from peeled lets
    while (const auto *le = dynamic_cast<const LetExpr *>(cur)) {
        block.stmts.push_back({le->var, expr_to_cexpr(le->init.get())});
        cur = le->body.get();
    }
    block.ret = expr_to_cexpr(cur);

    cir::CProgram result;
    result.blocks["start"] = std::move(block);
    return result;
}
