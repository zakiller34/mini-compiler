#include "explicate_control.h"

#include <map>
#include <string>
#include <vector>

namespace {

/// @brief Generate unique label with prefix
/// @requires label_counter >= 0
/// @ensures result = prefix + "_" + N, label_counter incremented
std::string fresh_label(const std::string &prefix, int &label_counter) {
    return prefix + "_" + std::to_string(label_counter++);
}

/// @brief Convert leaf/atomic expr to CIR Atom
/// @requires e is Int/Bool/Var/Get/Void (atomic after RCO)
/// @ensures result is corresponding CIR Atom
cir::Atom make_atom(const Expr *e) {
    switch (e->kind()) {
    case NodeKind::Int:
        return cir::IntAtom{static_cast<const IntExpr *>(e)->value};
    case NodeKind::Bool:
        return cir::BoolAtom{static_cast<const BoolExpr *>(e)->value};
    case NodeKind::Get:
        return cir::VarAtom{static_cast<const GetExpr *>(e)->name};
    case NodeKind::Void:
        return cir::IntAtom{0};
    case NodeKind::Var:
        return cir::VarAtom{static_cast<const VarExpr *>(e)->name};
    default:
        return cir::VarAtom{static_cast<const VarExpr *>(e)->name};
    }
}

/// @brief Convert AST expr to CExpr (for atomic/simple exprs after RCO)
/// @requires e is atomic or simple (Unary/Binary with atomic operands)
/// @ensures result is corresponding CIR CExpr
cir::CExpr expr_to_cexpr(const Expr *e) {
    switch (e->kind()) {
    case NodeKind::Int:
        return cir::AtomExpr{cir::IntAtom{
            static_cast<const IntExpr *>(e)->value}};
    case NodeKind::Bool:
        return cir::AtomExpr{cir::BoolAtom{
            static_cast<const BoolExpr *>(e)->value}};
    case NodeKind::Var:
        return cir::AtomExpr{cir::VarAtom{
            static_cast<const VarExpr *>(e)->name}};
    case NodeKind::Get:
        return cir::AtomExpr{cir::VarAtom{
            static_cast<const GetExpr *>(e)->name}};
    case NodeKind::Void:
        return cir::AtomExpr{cir::IntAtom{0}};
    case NodeKind::Read:
        return cir::CReadExpr{};
    case NodeKind::Unary: {
        auto *ue = static_cast<const UnaryExpr *>(e);
        if (ue->op == UnaryOp::Not) {
            return cir::CNotExpr{make_atom(ue->operand.get())};
        }
        return cir::CUnaryExpr{cir::CUnaryOp::Neg,
                                make_atom(ue->operand.get())};
    }
    case NodeKind::Binary: {
        auto *bine = static_cast<const BinaryExpr *>(e);
        if (bine->op == BinaryOp::Add || bine->op == BinaryOp::Sub) {
            auto op = (bine->op == BinaryOp::Add) ? cir::CBinaryOp::Add
                                                   : cir::CBinaryOp::Sub;
            return cir::CBinaryExpr{op, make_atom(bine->lhs.get()),
                                     make_atom(bine->rhs.get())};
        }
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
    default:
        return cir::AtomExpr{cir::IntAtom{0}};
    }
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
    switch (e->kind()) {
    case NodeKind::If: case NodeKind::While:
    case NodeKind::Begin: case NodeKind::SetBang:
        return true;
    default:
        return false;
    }
}

/// @brief Peel lets from expression, accumulating assignments
/// @ensures cur points to non-let terminal
void peel_lets(const Expr *&cur, std::vector<cir::Assign> &stmts) {
    // invariant: stmts has assignments from peeled lets
    // decreases: depth of let nesting
    while (cur->kind() == NodeKind::Let) {
        auto *le = static_cast<const LetExpr *>(cur);
        if (is_complex_init(le->init.get())) break;
        stmts.push_back({le->var, expr_to_cexpr(le->init.get())});
        cur = le->body.get();
    }
}

/// @brief Emit begin sub-exprs as effects, return label for last
/// @requires exprs non-empty, handles complex sub-exprs via worklist
/// @ensures simple sub-exprs added as stmts, complex ones via EffectWork
void emit_begin_effects(
    const std::vector<std::unique_ptr<Expr>> &exprs, size_t count,
    std::string &cur_label, std::vector<cir::Assign> &cur_stmts,
    std::vector<Work> &wl, int &lc) {
    // invariant: cur_stmts collects simple assigns for cur_label
    for (size_t i = 0; i < count; ++i) {
        const Expr *sub = exprs[i].get();
        if (is_complex_init(sub)) {
            std::string next_l = fresh_label("seq", lc);
            wl.push_back(EffectWork{cur_label,
                std::move(cur_stmts), sub, next_l});
            cur_label = next_l;
            cur_stmts = {};
        } else if (sub->kind() == NodeKind::SetBang) {
            auto *ss = static_cast<const SetBangExpr *>(sub);
            cur_stmts.push_back({ss->var_name,
                expr_to_cexpr(ss->expr.get())});
        } else {
            std::string dummy = fresh_label("_", lc);
            cur_stmts.push_back({dummy, expr_to_cexpr(sub)});
        }
    }
}

/// @brief Handle TailWork: expr in tail (return) position
/// @requires tw valid, blocks/worklist/lc by ref
/// @ensures new blocks emitted or work items pushed
void handle_tail_work(TailWork &tw, std::vector<Work> &wl,
                      std::map<std::string, cir::BasicBlock> &blocks,
                      int &lc) {
    auto stmts = std::move(tw.stmts);
    const Expr *cur = tw.expr;
    peel_lets(cur, stmts);

    switch (cur->kind()) {
    case NodeKind::If: {
        auto *ife = static_cast<const IfExpr *>(cur);
        std::string then_l = fresh_label("then", lc);
        std::string else_l = fresh_label("else", lc);
        wl.push_back(TailWork{then_l, {}, ife->then_branch.get()});
        wl.push_back(TailWork{else_l, {}, ife->else_branch.get()});
        wl.push_back(PredWork{ife->cond.get(), then_l, else_l,
                               tw.block_label, std::move(stmts)});
        break;
    }
    case NodeKind::Let: {
        auto *le = static_cast<const LetExpr *>(cur);
        std::string cont_l = fresh_label("cont", lc);
        wl.push_back(TailWork{cont_l, {}, le->body.get()});
        wl.push_back(AssignWork{tw.block_label, std::move(stmts),
                                le->init.get(), le->var, cont_l});
        break;
    }
    case NodeKind::While: {
        auto *we = static_cast<const WhileExpr *>(cur);
        std::string le = fresh_label("loop_entry", lc);
        std::string lb = fresh_label("loop_body", lc);
        std::string lx = fresh_label("loop_exit", lc);
        blocks[tw.block_label] = {std::move(stmts), cir::Goto{le}};
        wl.push_back(EffectWork{lb, {}, we->body.get(), le});
        wl.push_back(PredWork{we->cond.get(), lb, lx, le, {}});
        blocks[lx] = {{}, cir::Return{cir::AtomExpr{cir::IntAtom{0}}}};
        break;
    }
    case NodeKind::SetBang: {
        auto *se = static_cast<const SetBangExpr *>(cur);
        stmts.push_back({se->var_name, expr_to_cexpr(se->expr.get())});
        blocks[tw.block_label] = {std::move(stmts),
            cir::Return{cir::AtomExpr{cir::IntAtom{0}}}};
        break;
    }
    case NodeKind::Begin: {
        auto *beg = static_cast<const BeginExpr *>(cur);
        if (beg->exprs.empty()) {
            blocks[tw.block_label] = {std::move(stmts),
                cir::Return{cir::AtomExpr{cir::IntAtom{0}}}};
            break;
        }
        std::string cl = tw.block_label;
        auto cs = std::move(stmts);
        emit_begin_effects(beg->exprs, beg->exprs.size() - 1, cl, cs, wl, lc);
        const Expr *last = beg->exprs.back().get();
        peel_lets(last, cs);
        if (is_complex_init(last)) {
            wl.push_back(TailWork{cl, std::move(cs), last});
        } else {
            blocks[cl] = {std::move(cs), cir::Return{expr_to_cexpr(last)}};
        }
        break;
    }
    default:
        blocks[tw.block_label] = {std::move(stmts),
                                    cir::Return{expr_to_cexpr(cur)}};
        break;
    }
}

/// @brief Handle PredWork: expr in predicate (condition) position
/// @requires pw valid
/// @ensures IfStmt/Goto block emitted or work items pushed
/// @returns true if caller should continue (skip fallthrough default)
bool handle_pred_work(PredWork &pw, std::vector<Work> &wl,
                      std::map<std::string, cir::BasicBlock> &blocks,
                      int &lc) {
    const Expr *cond = pw.expr;
    switch (cond->kind()) {
    case NodeKind::Binary: {
        auto *bine = static_cast<const BinaryExpr *>(cond);
        if (bine->op == BinaryOp::Eq || bine->op == BinaryOp::Lt ||
            bine->op == BinaryOp::Le || bine->op == BinaryOp::Gt ||
            bine->op == BinaryOp::Ge) {
            blocks[pw.block_label] = {std::move(pw.stmts),
                cir::IfStmt{to_cmp_op(bine->op),
                             make_atom(bine->lhs.get()),
                             make_atom(bine->rhs.get()),
                             pw.then_label, pw.else_label}};
            return true;
        }
        break;
    }
    case NodeKind::Unary: {
        auto *ue = static_cast<const UnaryExpr *>(cond);
        if (ue->op == UnaryOp::Not) {
            wl.push_back(PredWork{ue->operand.get(),
                                   pw.else_label, pw.then_label,
                                   pw.block_label, std::move(pw.stmts)});
            return true;
        }
        break;
    }
    case NodeKind::Bool: {
        auto *be = static_cast<const BoolExpr *>(cond);
        blocks[pw.block_label] = {std::move(pw.stmts),
            cir::Goto{be->value ? pw.then_label : pw.else_label}};
        return true;
    }
    case NodeKind::If: {
        auto *ife = static_cast<const IfExpr *>(cond);
        std::string it = fresh_label("pthen", lc);
        std::string ie = fresh_label("pelse", lc);
        wl.push_back(PredWork{ife->then_branch.get(),
                               pw.then_label, pw.else_label, it, {}});
        wl.push_back(PredWork{ife->else_branch.get(),
                               pw.then_label, pw.else_label, ie, {}});
        wl.push_back(PredWork{ife->cond.get(), it, ie,
                               pw.block_label, std::move(pw.stmts)});
        return true;
    }
    default:
        break;
    }
    blocks[pw.block_label] = {std::move(pw.stmts),
        cir::IfStmt{cir::CCmpOp::Eq, make_atom(cond),
                     cir::BoolAtom{true},
                     pw.then_label, pw.else_label}};
    return false;
}

/// @brief Handle AssignWork: expr assigned to var, then goto cont
/// @requires aw valid
/// @ensures assignment block emitted or work items pushed
void handle_assign_work(AssignWork &aw, std::vector<Work> &wl,
                        std::map<std::string, cir::BasicBlock> &blocks,
                        int &lc) {
    const Expr *e = aw.expr;
    switch (e->kind()) {
    case NodeKind::If: {
        auto *ife = static_cast<const IfExpr *>(e);
        std::string tl = fresh_label("athen", lc);
        std::string el = fresh_label("aelse", lc);
        wl.push_back(AssignWork{tl, {}, ife->then_branch.get(),
                                 aw.var, aw.cont_label});
        wl.push_back(AssignWork{el, {}, ife->else_branch.get(),
                                 aw.var, aw.cont_label});
        wl.push_back(PredWork{ife->cond.get(), tl, el,
                               aw.block_label, std::move(aw.stmts)});
        break;
    }
    case NodeKind::While: {
        auto *we = static_cast<const WhileExpr *>(e);
        std::string le = fresh_label("loop_entry", lc);
        std::string lb = fresh_label("loop_body", lc);
        std::string lx = fresh_label("loop_exit", lc);
        blocks[aw.block_label] = {std::move(aw.stmts), cir::Goto{le}};
        wl.push_back(EffectWork{lb, {}, we->body.get(), le});
        wl.push_back(PredWork{we->cond.get(), lb, lx, le, {}});
        blocks[lx] = {{{aw.var, cir::AtomExpr{cir::IntAtom{0}}}},
                       cir::Goto{aw.cont_label}};
        break;
    }
    case NodeKind::Begin: {
        auto *beg = static_cast<const BeginExpr *>(e);
        auto cs = std::move(aw.stmts);
        std::string cl = aw.block_label;
        emit_begin_effects(beg->exprs, beg->exprs.size() > 0
            ? beg->exprs.size() - 1 : 0, cl, cs, wl, lc);
        const Expr *last = beg->exprs.empty()
            ? nullptr : beg->exprs.back().get();
        if (last == nullptr) {
            cs.push_back({aw.var, cir::AtomExpr{cir::IntAtom{0}}});
            blocks[cl] = {std::move(cs), cir::Goto{aw.cont_label}};
        } else if (is_complex_init(last)) {
            wl.push_back(AssignWork{cl, std::move(cs), last,
                                     aw.var, aw.cont_label});
        } else {
            cs.push_back({aw.var, expr_to_cexpr(last)});
            blocks[cl] = {std::move(cs), cir::Goto{aw.cont_label}};
        }
        break;
    }
    case NodeKind::SetBang: {
        auto *se = static_cast<const SetBangExpr *>(e);
        auto stmts = std::move(aw.stmts);
        stmts.push_back({se->var_name, expr_to_cexpr(se->expr.get())});
        stmts.push_back({aw.var, cir::AtomExpr{cir::IntAtom{0}}});
        blocks[aw.block_label] = {std::move(stmts),
                                   cir::Goto{aw.cont_label}};
        break;
    }
    default: {
        auto stmts = std::move(aw.stmts);
        stmts.push_back({aw.var, expr_to_cexpr(e)});
        blocks[aw.block_label] = {std::move(stmts),
                                   cir::Goto{aw.cont_label}};
        break;
    }
    }
}

/// @brief Handle EffectWork: expr evaluated for side-effect, goto cont
/// @requires ew valid
/// @ensures effect block emitted or work items pushed
void handle_effect_work(EffectWork &ew, std::vector<Work> &wl,
                        std::map<std::string, cir::BasicBlock> &blocks,
                        int &lc) {
    const Expr *e = ew.expr;
    auto stmts = std::move(ew.stmts);
    peel_lets(e, stmts);

    switch (e->kind()) {
    case NodeKind::SetBang: {
        auto *se = static_cast<const SetBangExpr *>(e);
        stmts.push_back({se->var_name, expr_to_cexpr(se->expr.get())});
        blocks[ew.block_label] = {std::move(stmts),
                                   cir::Goto{ew.cont_label}};
        break;
    }
    case NodeKind::Begin: {
        auto *beg = static_cast<const BeginExpr *>(e);
        std::string cl = ew.block_label;
        auto cs = std::move(stmts);
        emit_begin_effects(beg->exprs, beg->exprs.size(), cl, cs, wl, lc);
        blocks[cl] = {std::move(cs), cir::Goto{ew.cont_label}};
        break;
    }
    case NodeKind::While: {
        auto *we = static_cast<const WhileExpr *>(e);
        std::string le = fresh_label("loop_entry", lc);
        std::string lb = fresh_label("loop_body", lc);
        std::string lx = fresh_label("loop_exit", lc);
        blocks[ew.block_label] = {std::move(stmts), cir::Goto{le}};
        wl.push_back(EffectWork{lb, {}, we->body.get(), le});
        wl.push_back(PredWork{we->cond.get(), lb, lx, le, {}});
        blocks[lx] = {{}, cir::Goto{ew.cont_label}};
        break;
    }
    case NodeKind::If: {
        auto *ife = static_cast<const IfExpr *>(e);
        std::string tl = fresh_label("ethen", lc);
        std::string el = fresh_label("eelse", lc);
        wl.push_back(EffectWork{tl, {}, ife->then_branch.get(),
                                 ew.cont_label});
        wl.push_back(EffectWork{el, {}, ife->else_branch.get(),
                                 ew.cont_label});
        wl.push_back(PredWork{ife->cond.get(), tl, el,
                               ew.block_label, std::move(stmts)});
        break;
    }
    default: {
        std::string dummy = fresh_label("_", lc);
        stmts.push_back({dummy, expr_to_cexpr(e)});
        blocks[ew.block_label] = {std::move(stmts),
                                   cir::Goto{ew.cont_label}};
        break;
    }
    }
}

/// @brief Worklist loop: pop work, dispatch to handler
/// @requires worklist has initial TailWork for "start"
/// @ensures blocks populated with all generated basic blocks
void process_work(std::vector<Work> &worklist,
                  std::map<std::string, cir::BasicBlock> &blocks,
                  int &lc) {
    // decreases: worklist.size() + complexity of remaining exprs
    while (!worklist.empty()) {
        auto work = std::move(worklist.back());
        worklist.pop_back();
        if (auto *tw = std::get_if<TailWork>(&work)) {
            handle_tail_work(*tw, worklist, blocks, lc);
        } else if (auto *pw = std::get_if<PredWork>(&work)) {
            handle_pred_work(*pw, worklist, blocks, lc);
        } else if (auto *aw = std::get_if<AssignWork>(&work)) {
            handle_assign_work(*aw, worklist, blocks, lc);
        } else if (auto *ew = std::get_if<EffectWork>(&work)) {
            handle_effect_work(*ew, worklist, blocks, lc);
        }
    }
}

} // namespace

/// @brief Explicate control: AST -> C_Var IR with basic blocks
/// @requires prog.body != nullptr (after shrink + uniquify + uncover_get + RCO)
/// @ensures result.blocks has "start" block; tails are Goto/IfStmt/Return
cir::CProgram explicate_control(const Program &prog) {
    int label_counter = 0;
    std::map<std::string, cir::BasicBlock> blocks;
    std::vector<Work> worklist;

    worklist.push_back(TailWork{"start", {}, prog.body.get()});
    process_work(worklist, blocks, label_counter);

    return cir::CProgram{std::move(blocks)};
}
