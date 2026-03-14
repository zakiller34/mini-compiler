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

/// @brief Push tasks for unary expression
/// @requires ue != nullptr, ue->operand != nullptr
void push_unary(const UnaryExpr *ue, std::string &out,
                std::vector<Task> &tasks) {
  out += "(- ";
  tasks.push_back({Action::Append, nullptr, ")"});
  tasks.push_back({Action::Visit, ue->operand.get(), ""});
}

/// @brief Push tasks for binary expression
/// @requires be != nullptr, be->lhs/rhs != nullptr
void push_binary(const BinaryExpr *be, std::string &out,
                 std::vector<Task> &tasks) {
  out += (be->op == BinaryOp::Add) ? "(+ " : "(- ";
  tasks.push_back({Action::Append, nullptr, ")"});
  tasks.push_back({Action::Visit, be->rhs.get(), ""});
  tasks.push_back({Action::Append, nullptr, " "});
  tasks.push_back({Action::Visit, be->lhs.get(), ""});
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
  } else if (const auto *ve = dynamic_cast<const VarExpr *>(e)) {
    out += ve->name;
  } else if (dynamic_cast<const ReadExpr *>(e) != nullptr) {
    out += "(read)";
  } else if (const auto *ue = dynamic_cast<const UnaryExpr *>(e)) {
    push_unary(ue, out, tasks);
  } else if (const auto *be = dynamic_cast<const BinaryExpr *>(e)) {
    push_binary(be, out, tasks);
  } else if (const auto *le = dynamic_cast<const LetExpr *>(e)) {
    push_let(le, out, tasks);
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

std::string VarExpr::dump() const { return dump_iterative(this); }

std::string ReadExpr::dump() const { return dump_iterative(this); }

std::string UnaryExpr::dump() const { return dump_iterative(this); }

std::string BinaryExpr::dump() const { return dump_iterative(this); }

std::string LetExpr::dump() const { return dump_iterative(this); }

/// @brief Dump program as S-expr: (program body)
/// @requires body != nullptr
std::string Program::dump() const {
  return "(program " + dump_iterative(body.get()) + ")";
}
