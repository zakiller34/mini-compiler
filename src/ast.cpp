#include "ast.h"

#include <string>
#include <variant>
#include <vector>

namespace mc {

namespace {

enum class Action { Visit, Append };

struct Task {
  Action action;
  const Expr *expr;
  std::string text;
};

/// @brief Get S-expr name for a unary op
const char *unary_op_name(UnaryOp op) {
    return (op == UnaryOp::Neg) ? "-" : "not";
}

/// @brief Get S-expr name for a binary op
const char *binary_op_name(BinaryOp op) {
    switch (op) {
    case BinaryOp::Add: return "+";
    case BinaryOp::Sub: return "-";
    case BinaryOp::And: return "and";
    case BinaryOp::Or: return "or";
    case BinaryOp::Eq: return "==";
    case BinaryOp::Lt: return "<";
    case BinaryOp::Le: return "<=";
    case BinaryOp::Gt: return ">";
    case BinaryOp::Ge: return ">=";
    }
    return "?";
}

/// @brief Push tasks for unary expression
/// @requires ue != nullptr, ue->operand != nullptr
void push_unary(const UnaryExpr *ue, std::string &out,
                std::vector<Task> &tasks) {
  out += std::string("(") + unary_op_name(ue->op) + " ";
  tasks.push_back({Action::Append, nullptr, ")"});
  tasks.push_back({Action::Visit, ue->operand.get(), ""});
}

/// @brief Push tasks for binary expression
/// @requires be != nullptr, be->lhs/rhs != nullptr
void push_binary(const BinaryExpr *be, std::string &out,
                 std::vector<Task> &tasks) {
  out += std::string("(") + binary_op_name(be->op) + " ";
  tasks.push_back({Action::Append, nullptr, ")"});
  tasks.push_back({Action::Visit, be->rhs.get(), ""});
  tasks.push_back({Action::Append, nullptr, " "});
  tasks.push_back({Action::Visit, be->lhs.get(), ""});
}

/// @brief Push tasks for if expression
/// @requires ie != nullptr
void push_if(const IfExpr *ie, std::string &out,
             std::vector<Task> &tasks) {
  out += "(if ";
  tasks.push_back({Action::Append, nullptr, ")"});
  tasks.push_back({Action::Visit, ie->else_branch.get(), ""});
  tasks.push_back({Action::Append, nullptr, " "});
  tasks.push_back({Action::Visit, ie->then_branch.get(), ""});
  tasks.push_back({Action::Append, nullptr, " "});
  tasks.push_back({Action::Visit, ie->cond.get(), ""});
}

/// @brief Push tasks for let expression
/// @requires le != nullptr, le->init/body != nullptr
void push_let(const LetExpr *le, std::string &out,
              std::vector<Task> &tasks) {
  out += "(let ([" + le->var + " ";
  tasks.push_back({Action::Append, nullptr, ")"});
  tasks.push_back({Action::Visit, le->body.get(), ""});
  tasks.push_back({Action::Append, nullptr, "]) "});
  tasks.push_back({Action::Visit, le->init.get(), ""});
}

/// @brief Dispatch a single Visit: either append leaf or push children
/// @requires e != nullptr
void dispatch(const Expr *e, std::string &out, std::vector<Task> &tasks) {
  switch (e->kind()) {
  case NodeKind::Int:
    out += std::to_string(expr_cast<IntExpr>(e)->value);
    break;
  case NodeKind::Bool:
    out += expr_cast<BoolExpr>(e)->value ? "true" : "false";
    break;
  case NodeKind::Var:
    out += expr_cast<VarExpr>(e)->name;
    break;
  case NodeKind::Read:
    out += "(read)";
    break;
  case NodeKind::Unary:
    push_unary(expr_cast<UnaryExpr>(e), out, tasks);
    break;
  case NodeKind::Binary:
    push_binary(expr_cast<BinaryExpr>(e), out, tasks);
    break;
  case NodeKind::If:
    push_if(expr_cast<IfExpr>(e), out, tasks);
    break;
  case NodeKind::Let:
    push_let(expr_cast<LetExpr>(e), out, tasks);
    break;
  case NodeKind::While: {
    auto *we = expr_cast<WhileExpr>(e);
    out += "(while ";
    tasks.push_back({Action::Append, nullptr, ")"});
    tasks.push_back({Action::Visit, we->body.get(), ""});
    tasks.push_back({Action::Append, nullptr, " "});
    tasks.push_back({Action::Visit, we->cond.get(), ""});
    break;
  }
  case NodeKind::SetBang: {
    auto *se = expr_cast<SetBangExpr>(e);
    out += "(set! " + se->var_name + " ";
    tasks.push_back({Action::Append, nullptr, ")"});
    tasks.push_back({Action::Visit, se->expr.get(), ""});
    break;
  }
  case NodeKind::Begin: {
    auto *beg = expr_cast<BeginExpr>(e);
    out += "(begin";
    tasks.push_back({Action::Append, nullptr, ")"});
    for (int i = static_cast<int>(beg->exprs.size()) - 1; i >= 0; --i) {
      tasks.push_back({Action::Visit, beg->exprs[i].get(), ""});
      tasks.push_back({Action::Append, nullptr, " "});
    }
    break;
  }
  case NodeKind::Void:
    out += "(void)";
    break;
  case NodeKind::Get:
    out += "(get! " + expr_cast<GetExpr>(e)->name + ")";
    break;
  case NodeKind::Vector: {
    auto *ve = expr_cast<VectorExpr>(e);
    out += "(vector";
    tasks.push_back({Action::Append, nullptr, ")"});
    for (int i = static_cast<int>(ve->elems.size()) - 1; i >= 0; --i) {
      tasks.push_back({Action::Visit, ve->elems[i].get(), ""});
      tasks.push_back({Action::Append, nullptr, " "});
    }
    break;
  }
  case NodeKind::VectorRef: {
    auto *vr = expr_cast<VectorRefExpr>(e);
    out += "(vector-ref ";
    tasks.push_back({Action::Append, nullptr,
        " " + std::to_string(vr->index) + ")"});
    tasks.push_back({Action::Visit, vr->vec.get(), ""});
    break;
  }
  case NodeKind::VectorSet: {
    auto *vs = expr_cast<VectorSetExpr>(e);
    out += "(vector-set! ";
    tasks.push_back({Action::Append, nullptr, ")"});
    tasks.push_back({Action::Visit, vs->val.get(), ""});
    tasks.push_back({Action::Append, nullptr,
        " " + std::to_string(vs->index) + " "});
    tasks.push_back({Action::Visit, vs->vec.get(), ""});
    break;
  }
  case NodeKind::VectorLength: {
    auto *vl = expr_cast<VectorLengthExpr>(e);
    out += "(vector-length ";
    tasks.push_back({Action::Append, nullptr, ")"});
    tasks.push_back({Action::Visit, vl->vec.get(), ""});
    break;
  }
  case NodeKind::Allocate: {
    auto *ae = expr_cast<AllocateExpr>(e);
    out += "(allocate " + std::to_string(ae->len) + " " +
           ae->type->dump() + ")";
    break;
  }
  case NodeKind::Collect: {
    auto *ce = expr_cast<CollectExpr>(e);
    out += "(collect " + std::to_string(ce->bytes) + ")";
    break;
  }
  case NodeKind::GlobalValue: {
    auto *gv = expr_cast<GlobalValueExpr>(e);
    out += "(global-value " + gv->name + ")";
    break;
  }
  case NodeKind::Apply: {
    auto *ae = expr_cast<ApplyExpr>(e);
    out += "(apply ";
    tasks.push_back({Action::Append, nullptr, ")"});
    for (int i = static_cast<int>(ae->args.size()) - 1; i >= 0; --i) {
      tasks.push_back({Action::Visit, ae->args[i].get(), ""});
      tasks.push_back({Action::Append, nullptr, " "});
    }
    tasks.push_back({Action::Visit, ae->func.get(), ""});
    break;
  }
  case NodeKind::FunRef: {
    auto *fr = expr_cast<FunRefExpr>(e);
    out += "(fun-ref " + fr->name + " " + std::to_string(fr->arity) + ")";
    break;
  }
  }
}

} // namespace

/// @brief Iterative S-expr dump for expression tree
/// @requires root != nullptr
/// @ensures result is valid S-expression string
static std::string dump_iterative(const Expr *root) {
  std::vector<Task> tasks;
  std::string result;
  tasks.push_back({Action::Visit, root, ""});

  // decreases tasks.size() (each Visit pops 1, pushes bounded children)
  // invariant result accumulates S-expr prefix of processed nodes
  while (!tasks.empty()) {
    auto task = std::move(tasks.back());
    tasks.pop_back();

    if (task.action == Action::Append) {
      result += task.text;
    } else {
      dispatch(task.expr, result, tasks);
    }
  }
  return result;
}

std::string IntExpr::dump() const { return dump_iterative(this); }

std::string BoolExpr::dump() const { return dump_iterative(this); }

std::string VarExpr::dump() const { return dump_iterative(this); }

std::string ReadExpr::dump() const { return dump_iterative(this); }

std::string UnaryExpr::dump() const { return dump_iterative(this); }

std::string BinaryExpr::dump() const { return dump_iterative(this); }

std::string IfExpr::dump() const { return dump_iterative(this); }

std::string LetExpr::dump() const { return dump_iterative(this); }

std::string WhileExpr::dump() const { return dump_iterative(this); }

std::string SetBangExpr::dump() const { return dump_iterative(this); }

std::string BeginExpr::dump() const { return dump_iterative(this); }

std::string VoidExpr::dump() const { return dump_iterative(this); }

std::string GetExpr::dump() const { return dump_iterative(this); }

std::string VectorExpr::dump() const { return dump_iterative(this); }

std::string VectorRefExpr::dump() const { return dump_iterative(this); }

std::string VectorSetExpr::dump() const { return dump_iterative(this); }

std::string VectorLengthExpr::dump() const { return dump_iterative(this); }

std::string AllocateExpr::dump() const { return dump_iterative(this); }

std::string CollectExpr::dump() const { return dump_iterative(this); }

std::string GlobalValueExpr::dump() const { return dump_iterative(this); }

std::string ApplyExpr::dump() const { return dump_iterative(this); }

std::string FunRefExpr::dump() const { return dump_iterative(this); }

std::string DefNode::dump() const {
  std::string result = "(def " + name + " (";
  // invariant: result has params[0..i) dumped
  for (size_t i = 0; i < params.size(); ++i) {
    if (i > 0) result += " ";
    result += "[" + params[i].first + " : " + params[i].second->dump() + "]";
  }
  result += ") : " + ret_type->dump() + " ";
  result += dump_iterative(body.get()) + ")";
  return result;
}

/// @brief Dump program as S-expr: (program body)
/// @requires body != nullptr
std::string Program::dump() const {
  std::string result;
  // invariant: result has defs[0..i) dumped
  for (const auto &def : defs) {
    result += def.dump() + "\n";
  }
  result += "(program " + dump_iterative(body.get()) + ")";
  return result;
}

} // namespace mc
