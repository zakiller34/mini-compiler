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
    if (const auto *ge = dynamic_cast<const GetExpr *>(e)) {
        return cir::VarAtom{ge->name};
    }
    if (dynamic_cast<const VoidExpr *>(e) != nullptr) {
        return cir::IntAtom{0};
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
    if (const auto *ge = dynamic_cast<const GetExpr *>(e)) {
        return cir::AtomExpr{cir::VarAtom{ge->name}};
    }
    if (dynamic_cast<const VoidExpr *>(e) != nullptr) {
        return cir::AtomExpr{cir::IntAtom{0}};
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

struct EffectWork {
    std::string block_label;
    std::vector<cir::Assign> stmts;
    const Expr *expr;
    std::string cont_label;
};

using Work = std::variant<TailWork, PredWork, AssignWork, EffectWork>;

/// @brief Check if expr needs complex handling (if, while, begin, set!)
bool is_complex_init(const Expr *e) {
    return dynamic_cast<const IfExpr *>(e) != nullptr ||
           dynamic_cast<const WhileExpr *>(e) != nullptr ||
           dynamic_cast<const BeginExpr *>(e) != nullptr ||
           dynamic_cast<const SetBangExpr *>(e) != nullptr;
}

/// @brief Peel lets from expression, accumulating assignments
/// @ensures cur points to non-let terminal
void peel_lets(const Expr *&cur, std::vector<cir::Assign> &stmts) {
    // invariant: stmts has assignments from peeled lets
    // decreases: depth of let nesting
    while (const auto *le = dynamic_cast<const LetExpr *>(cur)) {
        if (is_complex_init(le->init.get())) {
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
                std::string then_l = fresh_label("then");
                std::string else_l = fresh_label("else");
                worklist.push_back(TailWork{then_l, {},
                                             ife->then_branch.get()});
                worklist.push_back(TailWork{else_l, {},
                                             ife->else_branch.get()});
                worklist.push_back(PredWork{ife->cond.get(), then_l, else_l,
                                             tw->block_label, std::move(stmts)});
            } else if (const auto *le = dynamic_cast<const LetExpr *>(cur)) {
                // Let with complex init
                std::string cont_l = fresh_label("cont");
                worklist.push_back(TailWork{cont_l, {}, le->body.get()});
                worklist.push_back(AssignWork{tw->block_label, std::move(stmts),
                                              le->init.get(), le->var, cont_l});
            } else if (const auto *we = dynamic_cast<const WhileExpr *>(cur)) {
                // while(cond) body in tail position → result is void
                std::string loop_entry = fresh_label("loop_entry");
                std::string loop_body = fresh_label("loop_body");
                std::string loop_exit = fresh_label("loop_exit");
                // Current block → goto loop_entry
                blocks[tw->block_label] = {std::move(stmts),
                                            cir::Goto{loop_entry}};
                // loop_body → body as effect, goto loop_entry
                worklist.push_back(EffectWork{loop_body, {},
                                               we->body.get(), loop_entry});
                // loop_entry → pred(cond, loop_body, loop_exit)
                worklist.push_back(PredWork{we->cond.get(), loop_body,
                                             loop_exit, loop_entry, {}});
                // loop_exit → return void (int 0)
                blocks[loop_exit] = {{},
                    cir::Return{cir::AtomExpr{cir::IntAtom{0}}}};
            } else if (const auto *se = dynamic_cast<const SetBangExpr *>(cur)) {
                // set! in tail position: assign, return void
                stmts.push_back({se->var_name, expr_to_cexpr(se->expr.get())});
                blocks[tw->block_label] = {std::move(stmts),
                    cir::Return{cir::AtomExpr{cir::IntAtom{0}}}};
            } else if (const auto *beg = dynamic_cast<const BeginExpr *>(cur)) {
                if (beg->exprs.empty()) {
                    blocks[tw->block_label] = {std::move(stmts),
                        cir::Return{cir::AtomExpr{cir::IntAtom{0}}}};
                } else {
                    // Chain: non-last exprs as effects, last in tail
                    // If any non-last expr is complex, chain via EffectWork
                    std::string cur_label = tw->block_label;
                    auto cur_stmts = std::move(stmts);
                    for (size_t i = 0; i + 1 < beg->exprs.size(); ++i) {
                        const Expr *sub = beg->exprs[i].get();
                        if (is_complex_init(sub) ||
                            dynamic_cast<const WhileExpr *>(sub) != nullptr ||
                            dynamic_cast<const IfExpr *>(sub) != nullptr) {
                            std::string next_l = fresh_label("seq");
                            worklist.push_back(EffectWork{cur_label,
                                std::move(cur_stmts), sub, next_l});
                            cur_label = next_l;
                            cur_stmts = {};
                        } else if (const auto *ss =
                                dynamic_cast<const SetBangExpr *>(sub)) {
                            cur_stmts.push_back({ss->var_name,
                                expr_to_cexpr(ss->expr.get())});
                        } else {
                            std::string dummy = fresh_label("_");
                            cur_stmts.push_back({dummy, expr_to_cexpr(sub)});
                        }
                    }
                    const Expr *last = beg->exprs.back().get();
                    peel_lets(last, cur_stmts);
                    if (is_complex_init(last) ||
                        dynamic_cast<const WhileExpr *>(last) != nullptr) {
                        worklist.push_back(TailWork{cur_label,
                                                     std::move(cur_stmts), last});
                    } else {
                        blocks[cur_label] = {std::move(cur_stmts),
                            cir::Return{expr_to_cexpr(last)}};
                    }
                }
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
                std::string then_l = fresh_label("athen");
                std::string else_l = fresh_label("aelse");
                worklist.push_back(AssignWork{then_l, {}, ife->then_branch.get(),
                                               aw->var, aw->cont_label});
                worklist.push_back(AssignWork{else_l, {}, ife->else_branch.get(),
                                               aw->var, aw->cont_label});
                worklist.push_back(PredWork{ife->cond.get(), then_l, else_l,
                                             aw->block_label,
                                             std::move(aw->stmts)});
            } else if (const auto *we = dynamic_cast<const WhileExpr *>(e)) {
                // while in assign: run loop, assign void to var, goto cont
                std::string loop_entry = fresh_label("loop_entry");
                std::string loop_body = fresh_label("loop_body");
                std::string loop_exit = fresh_label("loop_exit");
                blocks[aw->block_label] = {std::move(aw->stmts),
                                            cir::Goto{loop_entry}};
                worklist.push_back(EffectWork{loop_body, {},
                                               we->body.get(), loop_entry});
                worklist.push_back(PredWork{we->cond.get(), loop_body,
                                             loop_exit, loop_entry, {}});
                // loop_exit: assign void (0) to var, goto cont
                blocks[loop_exit] = {
                    {{aw->var, cir::AtomExpr{cir::IntAtom{0}}}},
                    cir::Goto{aw->cont_label}};
            } else if (const auto *beg = dynamic_cast<const BeginExpr *>(e)) {
                auto cur_stmts = std::move(aw->stmts);
                std::string cur_label = aw->block_label;
                for (size_t i = 0; i + 1 < beg->exprs.size(); ++i) {
                    const Expr *sub = beg->exprs[i].get();
                    if (is_complex_init(sub) ||
                        dynamic_cast<const WhileExpr *>(sub) != nullptr ||
                        dynamic_cast<const IfExpr *>(sub) != nullptr) {
                        std::string next_l = fresh_label("seq");
                        worklist.push_back(EffectWork{cur_label,
                            std::move(cur_stmts), sub, next_l});
                        cur_label = next_l;
                        cur_stmts = {};
                    } else if (const auto *ss =
                            dynamic_cast<const SetBangExpr *>(sub)) {
                        cur_stmts.push_back({ss->var_name,
                            expr_to_cexpr(ss->expr.get())});
                    } else {
                        std::string dummy = fresh_label("_");
                        cur_stmts.push_back({dummy, expr_to_cexpr(sub)});
                    }
                }
                const Expr *last = beg->exprs.empty()
                    ? nullptr : beg->exprs.back().get();
                if (last == nullptr) {
                    cur_stmts.push_back({aw->var,
                        cir::AtomExpr{cir::IntAtom{0}}});
                    blocks[cur_label] = {std::move(cur_stmts),
                        cir::Goto{aw->cont_label}};
                } else if (is_complex_init(last) ||
                           dynamic_cast<const WhileExpr *>(last) != nullptr) {
                    worklist.push_back(AssignWork{cur_label,
                        std::move(cur_stmts), last, aw->var, aw->cont_label});
                } else {
                    cur_stmts.push_back({aw->var, expr_to_cexpr(last)});
                    blocks[cur_label] = {std::move(cur_stmts),
                        cir::Goto{aw->cont_label}};
                }
            } else if (const auto *se = dynamic_cast<const SetBangExpr *>(e)) {
                auto stmts = std::move(aw->stmts);
                stmts.push_back({se->var_name,
                    expr_to_cexpr(se->expr.get())});
                stmts.push_back({aw->var,
                    cir::AtomExpr{cir::IntAtom{0}}});
                blocks[aw->block_label] = {std::move(stmts),
                    cir::Goto{aw->cont_label}};
            } else {
                auto stmts = std::move(aw->stmts);
                stmts.push_back({aw->var, expr_to_cexpr(e)});
                blocks[aw->block_label] = {std::move(stmts),
                                            cir::Goto{aw->cont_label}};
            }
        } else if (auto *ew = std::get_if<EffectWork>(&work)) {
            const Expr *e = ew->expr;
            auto stmts = std::move(ew->stmts);
            peel_lets(e, stmts);

            if (const auto *se = dynamic_cast<const SetBangExpr *>(e)) {
                stmts.push_back({se->var_name,
                    expr_to_cexpr(se->expr.get())});
                blocks[ew->block_label] = {std::move(stmts),
                    cir::Goto{ew->cont_label}};
            } else if (const auto *beg = dynamic_cast<const BeginExpr *>(e)) {
                std::string cur_label = ew->block_label;
                auto cur_stmts = std::move(stmts);
                for (size_t i = 0; i < beg->exprs.size(); ++i) {
                    const Expr *sub = beg->exprs[i].get();
                    if (is_complex_init(sub) ||
                        dynamic_cast<const WhileExpr *>(sub) != nullptr ||
                        dynamic_cast<const IfExpr *>(sub) != nullptr) {
                        std::string next_l = fresh_label("seq");
                        worklist.push_back(EffectWork{cur_label,
                            std::move(cur_stmts), sub, next_l});
                        cur_label = next_l;
                        cur_stmts = {};
                    } else if (const auto *ss =
                            dynamic_cast<const SetBangExpr *>(sub)) {
                        cur_stmts.push_back({ss->var_name,
                            expr_to_cexpr(ss->expr.get())});
                    } else {
                        std::string dummy = fresh_label("_");
                        cur_stmts.push_back({dummy, expr_to_cexpr(sub)});
                    }
                }
                blocks[cur_label] = {std::move(cur_stmts),
                    cir::Goto{ew->cont_label}};
            } else if (const auto *we = dynamic_cast<const WhileExpr *>(e)) {
                // Nested while in effect position
                std::string loop_entry = fresh_label("loop_entry");
                std::string loop_body = fresh_label("loop_body");
                std::string loop_exit = fresh_label("loop_exit");
                blocks[ew->block_label] = {std::move(stmts),
                                            cir::Goto{loop_entry}};
                worklist.push_back(EffectWork{loop_body, {},
                                               we->body.get(), loop_entry});
                worklist.push_back(PredWork{we->cond.get(), loop_body,
                                             loop_exit, loop_entry, {}});
                blocks[loop_exit] = {{}, cir::Goto{ew->cont_label}};
            } else if (const auto *ife = dynamic_cast<const IfExpr *>(e)) {
                // If in effect position
                std::string then_l = fresh_label("ethen");
                std::string else_l = fresh_label("eelse");
                worklist.push_back(EffectWork{then_l, {},
                                               ife->then_branch.get(),
                                               ew->cont_label});
                worklist.push_back(EffectWork{else_l, {},
                                               ife->else_branch.get(),
                                               ew->cont_label});
                worklist.push_back(PredWork{ife->cond.get(), then_l, else_l,
                                             ew->block_label, std::move(stmts)});
            } else {
                // Simple effect: evaluate and discard
                std::string dummy = fresh_label("_");
                stmts.push_back({dummy, expr_to_cexpr(e)});
                blocks[ew->block_label] = {std::move(stmts),
                    cir::Goto{ew->cont_label}};
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
