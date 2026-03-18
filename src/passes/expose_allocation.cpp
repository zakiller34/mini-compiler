#include "expose_allocation.h"

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

namespace {

int tmp_counter = 0;

/// @brief Generate a fresh temporary name
std::string fresh_tmp() {
    return "alloc." + std::to_string(tmp_counter++);
}

struct EvalFrame { const Expr *expr; };
struct UnaryBuild { UnaryOp op; };
struct BinBuildLhs { BinaryOp op; const Expr *rhs; };
struct BinBuildRhs { BinaryOp op; };
struct IfBuildCond { const Expr *then_br; const Expr *else_br; };
struct IfBuildThen { const Expr *else_br; };
struct IfBuildElse {};
struct LetBuildInit { std::string var; const Expr *body; };
struct LetBuildBody { std::string var; };
struct WhileBuildCond { const Expr *body; };
struct WhileBuildBody {};
struct SetBangBuild { std::string var; };
struct BeginBuild { std::vector<const Expr *> remaining; size_t total; };
struct VectorBuild { size_t total; TypePtr vec_type; };
struct VectorRefBuild { int64_t index; };
struct VectorSetVecBuild { int64_t index; const Expr *val; };
struct VectorSetValBuild { int64_t index; };
struct VectorLengthBuild {};

using Frame = std::variant<EvalFrame, UnaryBuild, BinBuildLhs, BinBuildRhs,
                           IfBuildCond, IfBuildThen, IfBuildElse,
                           LetBuildInit, LetBuildBody,
                           WhileBuildCond, WhileBuildBody,
                           SetBangBuild, BeginBuild,
                           VectorBuild, VectorRefBuild,
                           VectorSetVecBuild, VectorSetValBuild,
                           VectorLengthBuild>;

/// @brief Infer the type of a vector literal for Allocate node
/// @requires elems non-empty; elements already expose-allocated
TypePtr infer_vector_type(const std::vector<std::unique_ptr<Expr>> &elems) {
    std::vector<TypePtr> ts;
    // invariant: ts has inferred types for elems[0..i)
    for (size_t i = 0; i < elems.size(); ++i) {
        const Expr *e = elems[i].get();
        switch (e->kind()) {
        case NodeKind::Int: ts.push_back(int_type()); break;
        case NodeKind::Bool: ts.push_back(bool_type()); break;
        case NodeKind::Void: ts.push_back(void_type()); break;
        case NodeKind::Allocate:
            ts.push_back(static_cast<const AllocateExpr *>(e)->type);
            break;
        default:
            // For vars and other exprs, we use Int as default
            // (type checker has validated; actual type threading is more complex)
            ts.push_back(int_type());
            break;
        }
    }
    return vector_type(std::move(ts));
}

void push_eval(const EvalFrame &ef, std::vector<Frame> &stack,
               std::vector<std::unique_ptr<Expr>> &results) {
    const Expr *e = ef.expr;

    switch (e->kind()) {
    case NodeKind::Int:
        results.push_back(std::make_unique<IntExpr>(
            static_cast<const IntExpr *>(e)->value));
        break;
    case NodeKind::Bool:
        results.push_back(std::make_unique<BoolExpr>(
            static_cast<const BoolExpr *>(e)->value));
        break;
    case NodeKind::Var:
        results.push_back(std::make_unique<VarExpr>(
            static_cast<const VarExpr *>(e)->name));
        break;
    case NodeKind::Read:
        results.push_back(std::make_unique<ReadExpr>());
        break;
    case NodeKind::Void:
        results.push_back(std::make_unique<VoidExpr>());
        break;
    case NodeKind::Get:
        results.push_back(std::make_unique<GetExpr>(
            static_cast<const GetExpr *>(e)->name));
        break;
    case NodeKind::Unary: {
        auto *ue = static_cast<const UnaryExpr *>(e);
        stack.push_back(UnaryBuild{ue->op});
        stack.push_back(EvalFrame{ue->operand.get()});
        break;
    }
    case NodeKind::Binary: {
        auto *be = static_cast<const BinaryExpr *>(e);
        stack.push_back(BinBuildLhs{be->op, be->rhs.get()});
        stack.push_back(EvalFrame{be->lhs.get()});
        break;
    }
    case NodeKind::If: {
        auto *ife = static_cast<const IfExpr *>(e);
        stack.push_back(IfBuildCond{ife->then_branch.get(),
                                     ife->else_branch.get()});
        stack.push_back(EvalFrame{ife->cond.get()});
        break;
    }
    case NodeKind::Let: {
        auto *le = static_cast<const LetExpr *>(e);
        stack.push_back(LetBuildInit{le->var, le->body.get()});
        stack.push_back(EvalFrame{le->init.get()});
        break;
    }
    case NodeKind::While: {
        auto *we = static_cast<const WhileExpr *>(e);
        stack.push_back(WhileBuildCond{we->body.get()});
        stack.push_back(EvalFrame{we->cond.get()});
        break;
    }
    case NodeKind::SetBang: {
        auto *se = static_cast<const SetBangExpr *>(e);
        stack.push_back(SetBangBuild{se->var_name});
        stack.push_back(EvalFrame{se->expr.get()});
        break;
    }
    case NodeKind::Begin: {
        auto *beg = static_cast<const BeginExpr *>(e);
        if (beg->exprs.empty()) {
            results.push_back(std::make_unique<BeginExpr>(
                std::vector<std::unique_ptr<Expr>>{}));
        } else {
            std::vector<const Expr *> remaining;
            for (size_t i = 1; i < beg->exprs.size(); ++i) {
                remaining.push_back(beg->exprs[i].get());
            }
            stack.push_back(BeginBuild{std::move(remaining),
                                        beg->exprs.size()});
            stack.push_back(EvalFrame{beg->exprs[0].get()});
        }
        break;
    }
    case NodeKind::Vector: {
        auto *ve = static_cast<const VectorExpr *>(e);
        // Push all elements for evaluation, then VectorBuild
        // We need a type but don't have it until we know element types
        // Use a placeholder; type is inferred after elements are built
        stack.push_back(VectorBuild{ve->elems.size(), nullptr});
        // Push elements in reverse
        for (int i = static_cast<int>(ve->elems.size()) - 1; i >= 0; --i) {
            stack.push_back(EvalFrame{ve->elems[i].get()});
        }
        break;
    }
    case NodeKind::VectorRef: {
        auto *vr = static_cast<const VectorRefExpr *>(e);
        stack.push_back(VectorRefBuild{vr->index});
        stack.push_back(EvalFrame{vr->vec.get()});
        break;
    }
    case NodeKind::VectorSet: {
        auto *vs = static_cast<const VectorSetExpr *>(e);
        stack.push_back(VectorSetVecBuild{vs->index, vs->val.get()});
        stack.push_back(EvalFrame{vs->vec.get()});
        break;
    }
    case NodeKind::VectorLength: {
        auto *vl = static_cast<const VectorLengthExpr *>(e);
        stack.push_back(VectorLengthBuild{});
        stack.push_back(EvalFrame{vl->vec.get()});
        break;
    }
    case NodeKind::Allocate:
        results.push_back(std::make_unique<AllocateExpr>(
            static_cast<const AllocateExpr *>(e)->len,
            static_cast<const AllocateExpr *>(e)->type));
        break;
    case NodeKind::Collect:
        results.push_back(std::make_unique<CollectExpr>(
            static_cast<const CollectExpr *>(e)->bytes));
        break;
    case NodeKind::GlobalValue:
        results.push_back(std::make_unique<GlobalValueExpr>(
            static_cast<const GlobalValueExpr *>(e)->name));
        break;
    }
}

void process_cont(Frame &frame, std::vector<Frame> &stack,
                  std::vector<std::unique_ptr<Expr>> &results) {
    if (auto *ub = std::get_if<UnaryBuild>(&frame)) {
        auto operand = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<UnaryExpr>(
            ub->op, std::move(operand)));
    } else if (auto *bl = std::get_if<BinBuildLhs>(&frame)) {
        stack.push_back(BinBuildRhs{bl->op});
        stack.push_back(EvalFrame{bl->rhs});
    } else if (auto *br = std::get_if<BinBuildRhs>(&frame)) {
        auto rhs = std::move(results.back()); results.pop_back();
        auto lhs = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<BinaryExpr>(
            br->op, std::move(lhs), std::move(rhs)));
    } else if (auto *ic = std::get_if<IfBuildCond>(&frame)) {
        stack.push_back(IfBuildThen{ic->else_br});
        stack.push_back(EvalFrame{ic->then_br});
    } else if (auto *it = std::get_if<IfBuildThen>(&frame)) {
        stack.push_back(IfBuildElse{});
        stack.push_back(EvalFrame{it->else_br});
    } else if (std::get_if<IfBuildElse>(&frame) != nullptr) {
        auto else_e = std::move(results.back()); results.pop_back();
        auto then_e = std::move(results.back()); results.pop_back();
        auto cond_e = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<IfExpr>(
            std::move(cond_e), std::move(then_e), std::move(else_e)));
    } else if (auto *li = std::get_if<LetBuildInit>(&frame)) {
        stack.push_back(LetBuildBody{li->var});
        stack.push_back(EvalFrame{li->body});
    } else if (auto *lb = std::get_if<LetBuildBody>(&frame)) {
        auto body = std::move(results.back()); results.pop_back();
        auto init = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<LetExpr>(
            lb->var, std::move(init), std::move(body)));
    } else if (auto *wc = std::get_if<WhileBuildCond>(&frame)) {
        stack.push_back(WhileBuildBody{});
        stack.push_back(EvalFrame{wc->body});
    } else if (std::get_if<WhileBuildBody>(&frame) != nullptr) {
        auto body = std::move(results.back()); results.pop_back();
        auto cond = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<WhileExpr>(
            std::move(cond), std::move(body)));
    } else if (auto *sb = std::get_if<SetBangBuild>(&frame)) {
        auto expr = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<SetBangExpr>(
            sb->var, std::move(expr)));
    } else if (auto *bb = std::get_if<BeginBuild>(&frame)) {
        if (bb->remaining.empty()) {
            std::vector<std::unique_ptr<Expr>> exprs;
            for (size_t i = 0; i < bb->total; ++i) {
                exprs.push_back(std::move(results.back()));
                results.pop_back();
            }
            std::reverse(exprs.begin(), exprs.end());
            results.push_back(std::make_unique<BeginExpr>(std::move(exprs)));
        } else {
            const Expr *next = bb->remaining[0];
            std::vector<const Expr *> rest(bb->remaining.begin() + 1,
                                            bb->remaining.end());
            stack.push_back(BeginBuild{std::move(rest), bb->total});
            stack.push_back(EvalFrame{next});
        }
    } else if (auto *vb = std::get_if<VectorBuild>(&frame)) {
        // All n elements are on results stack (in order)
        std::vector<std::unique_ptr<Expr>> elem_exprs;
        for (size_t i = 0; i < vb->total; ++i) {
            elem_exprs.push_back(std::move(results.back()));
            results.pop_back();
        }
        std::reverse(elem_exprs.begin(), elem_exprs.end());

        // Infer vector type from element expressions
        TypePtr vtype = infer_vector_type(elem_exprs);
        int64_t n = static_cast<int64_t>(vb->total);
        int64_t bytes = 8 * (n + 1); // tag + n elements

        // Build: let t0=e0 in ... let tn=en in
        //   begin { if (+ free_ptr bytes) < fromspace_end then void
        //           else collect(bytes);
        //           let v = allocate(n, type);
        //           vector-set!(v,0,t0); ... vector-set!(v,n-1,tn);
        //           v }

        // Generate temp names for each element
        std::vector<std::string> tmp_names;
        for (size_t i = 0; i < elem_exprs.size(); ++i) {
            tmp_names.push_back(fresh_tmp());
        }

        // Build the begin body
        std::vector<std::unique_ptr<Expr>> begin_body;

        // if (+ (global free_ptr) bytes) < (global fromspace_end)
        //   then void else collect(bytes)
        auto gc_check = std::make_unique<IfExpr>(
            std::make_unique<BinaryExpr>(
                BinaryOp::Lt,
                std::make_unique<BinaryExpr>(
                    BinaryOp::Add,
                    std::make_unique<GlobalValueExpr>("free_ptr"),
                    std::make_unique<IntExpr>(bytes)),
                std::make_unique<GlobalValueExpr>("fromspace_end")),
            std::make_unique<VoidExpr>(),
            std::make_unique<CollectExpr>(bytes));
        begin_body.push_back(std::move(gc_check));

        // let v = allocate(n, type)
        std::string v_name = fresh_tmp();

        // vector-set!(v, i, ti) for each element
        for (size_t i = 0; i < tmp_names.size(); ++i) {
            begin_body.push_back(std::make_unique<VectorSetExpr>(
                std::make_unique<VarExpr>(v_name),
                static_cast<int64_t>(i),
                std::make_unique<VarExpr>(tmp_names[i])));
        }

        // final: v
        begin_body.push_back(std::make_unique<VarExpr>(v_name));

        // Wrap in let v = allocate(n, type); begin { ... }
        std::unique_ptr<Expr> inner = std::make_unique<LetExpr>(
            v_name,
            std::make_unique<AllocateExpr>(n, vtype),
            std::make_unique<BeginExpr>(std::move(begin_body)));

        // Wrap in let ti = ei for each element (outermost first)
        for (int i = static_cast<int>(tmp_names.size()) - 1; i >= 0; --i) {
            inner = std::make_unique<LetExpr>(
                tmp_names[i], std::move(elem_exprs[i]), std::move(inner));
        }

        results.push_back(std::move(inner));
    } else if (auto *vr = std::get_if<VectorRefBuild>(&frame)) {
        auto vec = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<VectorRefExpr>(
            std::move(vec), vr->index));
    } else if (auto *vsv = std::get_if<VectorSetVecBuild>(&frame)) {
        stack.push_back(VectorSetValBuild{vsv->index});
        stack.push_back(EvalFrame{vsv->val});
    } else if (auto *vs = std::get_if<VectorSetValBuild>(&frame)) {
        auto val = std::move(results.back()); results.pop_back();
        auto vec = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<VectorSetExpr>(
            std::move(vec), vs->index, std::move(val)));
    } else if (std::get_if<VectorLengthBuild>(&frame) != nullptr) {
        auto vec = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<VectorLengthExpr>(std::move(vec)));
    }
}

} // namespace

std::unique_ptr<Program> expose_allocation(const Program &prog) {
    tmp_counter = 0;
    std::vector<Frame> stack;
    std::vector<std::unique_ptr<Expr>> results;
    stack.push_back(EvalFrame{prog.body.get()});

    // decreases: stack.size()
    while (!stack.empty()) {
        auto frame = std::move(stack.back());
        stack.pop_back();
        if (auto *ef = std::get_if<EvalFrame>(&frame)) {
            push_eval(*ef, stack, results);
        } else {
            process_cont(frame, stack, results);
        }
    }
    return std::make_unique<Program>(std::move(results.back()));
}
