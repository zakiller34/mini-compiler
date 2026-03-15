#include "explicate_control.h"

#include <map>
#include <string>
#include <vector>

namespace {

int label_counter = 0;

std::string fresh_label(const std::string &prefix) {
    return prefix + "_" + std::to_string(label_counter++);
}

/// @brief Convert leaf/atomic expr to CIR Atom
cir::Atom make_atom(const Expr *e) {
    if (const auto *ie = dynamic_cast<const IntExpr *>(e)) {
        return cir::IntAtom{ie->value};
    }
    if (const auto *be = dynamic_cast<const BoolExpr *>(e)) {
        return cir::BoolAtom{be->value};
    }
    return cir::VarAtom{dynamic_cast<const VarExpr *>(e)->name};
}

/// @brief Convert AST expr to CExpr (for atomic/simple exprs after RCO)
cir::CExpr expr_to_cexpr(const Expr *e) {
    if (const auto *ie = dynamic_cast<const IntExpr *>(e)) {
        return cir::AtomExpr{cir::IntAtom{ie->value}};
    }
    if (const auto *be = dynamic_cast<const BoolExpr *>(e)) {
        return cir::AtomExpr{cir::BoolAtom{be->value}};
    }
    if (const auto *ve = dynamic_cast<const VarExpr *>(e)) {
        return cir::AtomExpr{cir::VarAtom{ve->name}};
    }
    if (dynamic_cast<const ReadExpr *>(e) != nullptr) {
        return cir::CReadExpr{};
    }
    if (const auto *ue = dynamic_cast<const UnaryExpr *>(e)) {
        if (ue->op == UnaryOp::Not) {
            return cir::CNotExpr{make_atom(ue->operand.get())};
        }
        return cir::CUnaryExpr{cir::CUnaryOp::Neg,
                                make_atom(ue->operand.get())};
    }
    const auto *bine = dynamic_cast<const BinaryExpr *>(e);
    if (bine->op == BinaryOp::Add || bine->op == BinaryOp::Sub) {
        auto op = (bine->op == BinaryOp::Add) ? cir::CBinaryOp::Add
                                               : cir::CBinaryOp::Sub;
        return cir::CBinaryExpr{op, make_atom(bine->lhs.get()),
                                 make_atom(bine->rhs.get())};
    }
    // Comparison ops
    cir::CCmpOp cop{};
    switch (bine->op) {
    case BinaryOp::Eq: cop = cir::CCmpOp::Eq; break;
    case BinaryOp::Lt: cop = cir::CCmpOp::Lt; break;
    case BinaryOp::Le: cop = cir::CCmpOp::Le; break;
    case BinaryOp::Gt: cop = cir::CCmpOp::Gt; break;
    case BinaryOp::Ge: cop = cir::CCmpOp::Ge; break;
    default: break;
    }
    return cir::CCmpExpr{cop, make_atom(bine->lhs.get()),
                          make_atom(bine->rhs.get())};
}

/// @brief Convert BinaryOp to CCmpOp
cir::CCmpOp to_cmp_op(BinaryOp op) {
    switch (op) {
    case BinaryOp::Eq: return cir::CCmpOp::Eq;
    case BinaryOp::Lt: return cir::CCmpOp::Lt;
    case BinaryOp::Le: return cir::CCmpOp::Le;
    case BinaryOp::Gt: return cir::CCmpOp::Gt;
    case BinaryOp::Ge: return cir::CCmpOp::Ge;
    default: return cir::CCmpOp::Eq;
    }
}

// Forward declarations for mutual "recursion" via work items
struct TailWork {
    std::string block_label;
    std::vector<cir::Assign> stmts;
    const Expr *expr;
};

struct PredWork {
    const Expr *expr;
    std::string then_label;
    std::string else_label;
    std::string block_label;
    std::vector<cir::Assign> stmts;
};

struct AssignWork {
    std::string block_label;
    std::vector<cir::Assign> stmts;
    const Expr *expr;
    std::string var;
    std::string cont_label;
};

using Work = std::variant<TailWork, PredWork, AssignWork>;

/// @brief Peel lets from expression, accumulating assignments
/// @ensures cur points to non-let terminal
void peel_lets(const Expr *&cur, std::vector<cir::Assign> &stmts) {
    // invariant: stmts has assignments from peeled lets
    // decreases: depth of let nesting
    while (const auto *le = dynamic_cast<const LetExpr *>(cur)) {
        // Check if init is an IfExpr — needs special handling
        if (dynamic_cast<const IfExpr *>(le->init.get()) != nullptr) {
            break; // can't peel, need assign mode
        }
        stmts.push_back({le->var, expr_to_cexpr(le->init.get())});
        cur = le->body.get();
    }
}

/// @brief Process work items iteratively
void process_work(std::vector<Work> &worklist,
                  std::map<std::string, cir::BasicBlock> &blocks) {
    // decreases: worklist.size() + complexity of remaining exprs
    while (!worklist.empty()) {
        auto work = std::move(worklist.back());
        worklist.pop_back();

        if (auto *tw = std::get_if<TailWork>(&work)) {
            auto stmts = std::move(tw->stmts);
            const Expr *cur = tw->expr;
            peel_lets(cur, stmts);

            if (const auto *ife = dynamic_cast<const IfExpr *>(cur)) {
                // If in tail position: branches get own blocks
                std::string then_l = fresh_label("then");
                std::string else_l = fresh_label("else");

                worklist.push_back(TailWork{then_l, {},
                                             ife->then_branch.get()});
                worklist.push_back(TailWork{else_l, {},
                                             ife->else_branch.get()});
                worklist.push_back(PredWork{ife->cond.get(), then_l, else_l,
                                             tw->block_label, std::move(stmts)});
            } else if (const auto *le = dynamic_cast<const LetExpr *>(cur)) {
                // Let with IfExpr init
                std::string cont_l = fresh_label("cont");
                worklist.push_back(TailWork{cont_l, {}, le->body.get()});
                worklist.push_back(AssignWork{tw->block_label, std::move(stmts),
                                              le->init.get(), le->var, cont_l});
            } else {
                blocks[tw->block_label] = {std::move(stmts),
                                            cir::Return{expr_to_cexpr(cur)}};
            }
        } else if (auto *pw = std::get_if<PredWork>(&work)) {
            const Expr *cond = pw->expr;

            if (const auto *bine = dynamic_cast<const BinaryExpr *>(cond)) {
                if (bine->op == BinaryOp::Eq || bine->op == BinaryOp::Lt ||
                    bine->op == BinaryOp::Le || bine->op == BinaryOp::Gt ||
                    bine->op == BinaryOp::Ge) {
                    blocks[pw->block_label] = {
                        std::move(pw->stmts),
                        cir::IfStmt{to_cmp_op(bine->op),
                                     make_atom(bine->lhs.get()),
                                     make_atom(bine->rhs.get()),
                                     pw->then_label, pw->else_label}};
                    continue;
                }
            }
            if (const auto *ue = dynamic_cast<const UnaryExpr *>(cond)) {
                if (ue->op == UnaryOp::Not) {
                    // Swap then/else
                    worklist.push_back(PredWork{ue->operand.get(),
                                                 pw->else_label, pw->then_label,
                                                 pw->block_label,
                                                 std::move(pw->stmts)});
                    continue;
                }
            }
            if (const auto *be = dynamic_cast<const BoolExpr *>(cond)) {
                blocks[pw->block_label] = {
                    std::move(pw->stmts),
                    cir::Goto{be->value ? pw->then_label : pw->else_label}};
                continue;
            }
            if (const auto *ife = dynamic_cast<const IfExpr *>(cond)) {
                // Nested if in predicate: create blocks for inner branches
                std::string inner_then = fresh_label("pthen");
                std::string inner_else = fresh_label("pelse");
                worklist.push_back(PredWork{ife->then_branch.get(),
                                             pw->then_label, pw->else_label,
                                             inner_then, {}});
                worklist.push_back(PredWork{ife->else_branch.get(),
                                             pw->then_label, pw->else_label,
                                             inner_else, {}});
                worklist.push_back(PredWork{ife->cond.get(),
                                             inner_then, inner_else,
                                             pw->block_label,
                                             std::move(pw->stmts)});
                continue;
            }
            // Default: compare with true
            blocks[pw->block_label] = {
                std::move(pw->stmts),
                cir::IfStmt{cir::CCmpOp::Eq, make_atom(cond),
                             cir::BoolAtom{true},
                             pw->then_label, pw->else_label}};
        } else if (auto *aw = std::get_if<AssignWork>(&work)) {
            const Expr *e = aw->expr;
            if (const auto *ife = dynamic_cast<const IfExpr *>(e)) {
                // If in assign: both branches assign to var, goto cont
                std::string then_l = fresh_label("athen");
                std::string else_l = fresh_label("aelse");

                // Then branch: assign result to var, goto cont
                worklist.push_back(AssignWork{then_l, {}, ife->then_branch.get(),
                                               aw->var, aw->cont_label});
                worklist.push_back(AssignWork{else_l, {}, ife->else_branch.get(),
                                               aw->var, aw->cont_label});
                worklist.push_back(PredWork{ife->cond.get(), then_l, else_l,
                                             aw->block_label,
                                             std::move(aw->stmts)});
            } else {
                // Simple assign + goto cont
                auto stmts = std::move(aw->stmts);
                stmts.push_back({aw->var, expr_to_cexpr(e)});
                blocks[aw->block_label] = {std::move(stmts),
                                            cir::Goto{aw->cont_label}};
            }
        }
    }
}

} // namespace

/// @brief Explicate control: AST → C_If IR with basic blocks
/// @requires prog.body != nullptr (after uniquify + RCO + shrink)
/// @ensures result.blocks has "start" block and all generated blocks
cir::CProgram explicate_control(const Program &prog) {
    label_counter = 0;
    std::map<std::string, cir::BasicBlock> blocks;
    std::vector<Work> worklist;

    worklist.push_back(TailWork{"start", {}, prog.body.get()});
    process_work(worklist, blocks);

    return cir::CProgram{std::move(blocks)};
}
