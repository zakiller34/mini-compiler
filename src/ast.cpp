#include "ast.h"

#include <string>
#include <variant>
#include <vector>

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
  if (const auto *ie = dynamic_cast<const IntExpr *>(e)) {
    out += std::to_string(ie->value);
  } else if (const auto *be = dynamic_cast<const BoolExpr *>(e)) {
    out += be->value ? "true" : "false";
  } else if (const auto *ve = dynamic_cast<const VarExpr *>(e)) {
    out += ve->name;
  } else if (dynamic_cast<const ReadExpr *>(e) != nullptr) {
    out += "(read)";
  } else if (const auto *ue = dynamic_cast<const UnaryExpr *>(e)) {
    push_unary(ue, out, tasks);
  } else if (const auto *bine = dynamic_cast<const BinaryExpr *>(e)) {
    push_binary(bine, out, tasks);
  } else if (const auto *ife = dynamic_cast<const IfExpr *>(e)) {
    push_if(ife, out, tasks);
  } else if (const auto *le = dynamic_cast<const LetExpr *>(e)) {
    push_let(le, out, tasks);
  } else if (const auto *we = dynamic_cast<const WhileExpr *>(e)) {
    out += "(while ";
    tasks.push_back({Action::Append, nullptr, ")"});
    tasks.push_back({Action::Visit, we->body.get(), ""});
    tasks.push_back({Action::Append, nullptr, " "});
    tasks.push_back({Action::Visit, we->cond.get(), ""});
  } else if (const auto *se = dynamic_cast<const SetBangExpr *>(e)) {
    out += "(set! " + se->var_name + " ";
    tasks.push_back({Action::Append, nullptr, ")"});
    tasks.push_back({Action::Visit, se->expr.get(), ""});
  } else if (const auto *beg = dynamic_cast<const BeginExpr *>(e)) {
    out += "(begin";
    tasks.push_back({Action::Append, nullptr, ")"});
    for (int i = static_cast<int>(beg->exprs.size()) - 1; i >= 0; --i) {
      tasks.push_back({Action::Visit, beg->exprs[i].get(), ""});
      tasks.push_back({Action::Append, nullptr, " "});
    }
  } else if (dynamic_cast<const VoidExpr *>(e) != nullptr) {
    out += "(void)";
  } else if (const auto *ge = dynamic_cast<const GetExpr *>(e)) {
    out += "(get! " + ge->name + ")";
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

/// @brief Dump program as S-expr: (program body)
/// @requires body != nullptr
std::string Program::dump() const {
  return "(program " + dump_iterative(body.get()) + ")";
}
