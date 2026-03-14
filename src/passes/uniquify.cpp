#include "uniquify.h"

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace {

using RenameEnv = std::map<std::string, std::string>;

struct EvalFrame {
    const Expr *expr;
    RenameEnv env;
};

struct UnaryBuild {
    UnaryOp op;
};

struct BinBuildLhs {
    BinaryOp op;
    const Expr *rhs;
    RenameEnv env;
};

struct BinBuildRhs {
    BinaryOp op;
};

struct LetBuildInit {
    std::string old_var;
    std::string new_var;
    const Expr *body;
    RenameEnv env;
};

struct LetBuildBody {
    std::string new_var;
};

using Frame =
    std::variant<EvalFrame, UnaryBuild, BinBuildLhs, BinBuildRhs, LetBuildInit, LetBuildBody>;

/// @brief Handle eval frame: push leaf result or child frames
/// @requires ef.expr != nullptr
void push_eval(const EvalFrame &ef, std::vector<Frame> &stack,
               std::vector<std::unique_ptr<Expr>> &results, int &counter) {
    const Expr *e = ef.expr;
    const auto &env = ef.env;

    if (const auto *ie = dynamic_cast<const IntExpr *>(e)) {
        results.push_back(std::make_unique<IntExpr>(ie->value));
    } else if (const auto *ve = dynamic_cast<const VarExpr *>(e)) {
        auto it = env.find(ve->name);
        std::string name = (it != env.end()) ? it->second : ve->name;
        results.push_back(std::make_unique<VarExpr>(name));
    } else if (dynamic_cast<const ReadExpr *>(e) != nullptr) {
        results.push_back(std::make_unique<ReadExpr>());
    } else if (const auto *ue = dynamic_cast<const UnaryExpr *>(e)) {
        stack.push_back(UnaryBuild{ue->op});
        stack.push_back(EvalFrame{ue->operand.get(), env});
    } else if (const auto *be = dynamic_cast<const BinaryExpr *>(e)) {
        stack.push_back(BinBuildLhs{be->op, be->rhs.get(), env});
        stack.push_back(EvalFrame{be->lhs.get(), env});
    } else if (const auto *le = dynamic_cast<const LetExpr *>(e)) {
        std::string new_name = le->var + "." + std::to_string(counter++);
        stack.push_back(LetBuildInit{le->var, new_name, le->body.get(), env});
        stack.push_back(EvalFrame{le->init.get(), env});
    }
}

/// @brief Process continuation frame, building AST nodes from results
/// @requires results has enough elements for the frame type
void process_cont(Frame &frame, std::vector<Frame> &stack,
                  std::vector<std::unique_ptr<Expr>> &results) {
    if (auto *ub = std::get_if<UnaryBuild>(&frame)) {
        auto operand = std::move(results.back());
        results.pop_back();
        results.push_back(std::make_unique<UnaryExpr>(ub->op, std::move(operand)));
    } else if (auto *bl = std::get_if<BinBuildLhs>(&frame)) {
        stack.push_back(BinBuildRhs{bl->op});
        stack.push_back(EvalFrame{bl->rhs, bl->env});
    } else if (std::get_if<BinBuildRhs>(&frame) != nullptr) {
        auto &br = std::get<BinBuildRhs>(frame);
        auto rhs = std::move(results.back());
        results.pop_back();
        auto lhs = std::move(results.back());
        results.pop_back();
        results.push_back(
            std::make_unique<BinaryExpr>(br.op, std::move(lhs), std::move(rhs)));
    } else if (auto *li = std::get_if<LetBuildInit>(&frame)) {
        RenameEnv new_env = li->env;
        new_env[li->old_var] = li->new_var;
        stack.push_back(LetBuildBody{li->new_var});
        stack.push_back(EvalFrame{li->body, std::move(new_env)});
    } else if (auto *lb = std::get_if<LetBuildBody>(&frame)) {
        auto body = std::move(results.back());
        results.pop_back();
        auto init = std::move(results.back());
        results.pop_back();
        results.push_back(
            std::make_unique<LetExpr>(lb->new_var, std::move(init), std::move(body)));
    }
}

} // namespace

/// @brief Alpha-rename all variables to unique counter-based names
/// @requires prog.body != nullptr
/// @ensures all bound variables have unique names
std::unique_ptr<Program> uniquify(const Program &prog) {
    std::vector<Frame> stack;
    std::vector<std::unique_ptr<Expr>> results;
    int counter = 1;

    stack.push_back(EvalFrame{prog.body.get(), {}});

    // decreases stack.size() (each iteration pops 1, pushes bounded frames)
    // invariant results holds fully-built AST subtrees
    while (!stack.empty()) {
        auto frame = std::move(stack.back());
        stack.pop_back();

        if (auto *ef = std::get_if<EvalFrame>(&frame)) {
            push_eval(*ef, stack, results, counter);
        } else {
            process_cont(frame, stack, results);
        }
    }
    return std::make_unique<Program>(std::move(results.back()));
}
