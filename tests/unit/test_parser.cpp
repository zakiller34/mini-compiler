#include <gtest/gtest.h>

#include <memory>
#include <string>

#include <sstream>

#include "ast.h"
#include "lexer.h"
#include "parser.h"
#include "type_checker.h"

using namespace mc;

// Test AST construction + dump() output (S-expr format).
// Builds ASTs manually — no parser dependency.

TEST(ParserDump, IntLiteral) {
  auto e = std::make_unique<IntExpr>(42);
  EXPECT_EQ(e->dump(), "42");
}

TEST(ParserDump, Negation) {
  auto e = std::make_unique<UnaryExpr>(
      UnaryOp::Neg, std::make_unique<IntExpr>(10));
  EXPECT_EQ(e->dump(), "(- 10)");
}

TEST(ParserDump, Add) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<IntExpr>(10),
      std::make_unique<IntExpr>(32));
  EXPECT_EQ(e->dump(), "(+ 10 32)");
}

TEST(ParserDump, Sub) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Sub,
      std::make_unique<IntExpr>(52),
      std::make_unique<IntExpr>(10));
  EXPECT_EQ(e->dump(), "(- 52 10)");
}

TEST(ParserDump, Read) {
  auto e = std::make_unique<ReadExpr>();
  EXPECT_EQ(e->dump(), "(read)");
}

TEST(ParserDump, Var) {
  auto e = std::make_unique<VarExpr>("x");
  EXPECT_EQ(e->dump(), "x");
}

TEST(ParserDump, LetSimple) {
  auto e = std::make_unique<LetExpr>(
      "x",
      std::make_unique<IntExpr>(32),
      std::make_unique<BinaryExpr>(
          BinaryOp::Add,
          std::make_unique<IntExpr>(10),
          std::make_unique<VarExpr>("x")));
  EXPECT_EQ(e->dump(), "(let ([x 32]) (+ 10 x))");
}

TEST(ParserDump, NestedLet) {
  // let x = 5; let y = 37; x + y
  auto inner = std::make_unique<LetExpr>(
      "y",
      std::make_unique<IntExpr>(37),
      std::make_unique<BinaryExpr>(
          BinaryOp::Add,
          std::make_unique<VarExpr>("x"),
          std::make_unique<VarExpr>("y")));
  auto e = std::make_unique<LetExpr>(
      "x",
      std::make_unique<IntExpr>(5),
      std::move(inner));
  EXPECT_EQ(e->dump(), "(let ([x 5]) (let ([y 37]) (+ x y)))");
}

TEST(ParserDump, ProgramWrap) {
  auto prog = Program(std::make_unique<IntExpr>(42));
  EXPECT_EQ(prog.dump(), "(program 42)");
}

// ---- Phase 6: function AST dump tests ----

TEST(ParserDump, FunRefDump) {
    auto e = std::make_unique<FunRefExpr>("foo", 2);
    EXPECT_EQ(e->dump(), "(fun-ref foo 2)");
}

TEST(ParserDump, ApplyDump) {
    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::make_unique<IntExpr>(1));
    args.push_back(std::make_unique<IntExpr>(2));
    auto e = std::make_unique<ApplyExpr>(
        std::make_unique<FunRefExpr>("add", 2), std::move(args));
    EXPECT_EQ(e->dump(), "(apply (fun-ref add 2) 1 2)");
}

TEST(ParserDump, DefNodeDump) {
    DefNode d;
    d.name = "foo";
    d.params = {{"x", int_type()}};
    d.ret_type = int_type();
    d.body = std::make_unique<VarExpr>("x");
    // Check dump does not crash and produces non-empty output
    EXPECT_FALSE(d.dump().empty());
}

// ---- Phase 8: --dyn grammar and the L_Any surface forms ----

namespace {

/// @brief Parse `src` in static or dynamic mode
std::unique_ptr<Program> parse(const std::string &src, bool dyn) {
    std::istringstream in(src);
    Lexer lex(in);
    Parser parser(lex, dyn);
    return parser.parse_program();
}

} // namespace

TEST(ParserDyn, FnWithoutAnnotationsParsesAndBindsAny) {
    auto p = parse("fn id(v) { v }\nid(1)", true);
    ASSERT_EQ(p->defs.size(), 1U);
    EXPECT_TRUE(is_any_type(p->defs[0].params[0].second));
    EXPECT_TRUE(is_any_type(p->defs[0].ret_type));
}

TEST(ParserDyn, LambdaWithoutAnnotationsParses) {
    auto p = parse("lambda (x) { x }", true);
    ASSERT_EQ(p->body->kind(), NodeKind::Lambda);
    const auto *la = expr_cast<LambdaExpr>(p->body.get());
    EXPECT_TRUE(is_any_type(la->params[0].second));
    EXPECT_TRUE(is_any_type(la->ret_type));
}

TEST(ParserDyn, AnnotationsAreRejectedInDynMode) {
    EXPECT_THROW(parse("fn f(x:Int) : Int { x }\n1", true), ParseError);
    EXPECT_THROW(parse("lambda (x:Int) : Int { x }", true), ParseError);
}

TEST(ParserDyn, AnnotationsAreStillRequiredInStaticMode) {
    EXPECT_THROW(parse("fn f(x) { x }\n1", false), ParseError);
    EXPECT_NO_THROW(parse("fn f(x:Int) : Int { x }\n1", false));
}

TEST(ParserDyn, SubscriptTakesAnExpressionInDynMode) {
    auto p = parse("let t = vector(1, 2);\nt[0 + 1]", true);
    // let t = ...; t[...]
    ASSERT_EQ(p->body->kind(), NodeKind::Let);
    const auto *body = expr_cast<LetExpr>(p->body.get())->body.get();
    ASSERT_EQ(body->kind(), NodeKind::AnyVectorRef);
    EXPECT_EQ(expr_cast<AnyVectorRefExpr>(body)->idx->kind(),
              NodeKind::Binary);
}

TEST(ParserDyn, SubscriptStillNeedsALiteralInStaticMode) {
    EXPECT_THROW(parse("let t = vector(1, 2);\nt[0 + 1]", false), ParseError);
}

TEST(ParserAny, StaticModeParsesInjectProjectAndPredicates) {
    auto p = parse("inject(1, Int)", false);
    ASSERT_EQ(p->body->kind(), NodeKind::Inject);
    EXPECT_EQ(*expr_cast<InjectExpr>(p->body.get())->ftype, *int_type());

    auto q = parse("project(inject(1, Int), Int)", false);
    EXPECT_EQ(q->body->kind(), NodeKind::Project);

    auto r = parse("integer?(inject(1, Int))", false);
    ASSERT_EQ(r->body->kind(), NodeKind::TypePredicate);
    EXPECT_EQ(expr_cast<TypePredExpr>(r->body.get())->pred,
              TypePred::Integer);
}

TEST(ParserAny, InjectRejectsANonFlatType) {
    EXPECT_THROW(parse("inject(1, Any)", false), ParseError);
}

TEST(ParserAny, AnyIsAWritableTypeAnnotation) {
    auto p = parse("fn f(x:Any) : Any { x }\n1", false);
    EXPECT_TRUE(is_any_type(p->defs[0].params[0].second));
}

// --- Source locations (cross-cutting: error reporting) ---

namespace {

/// Parse and return the body, or nullptr on a parse error.
std::unique_ptr<Expr> parse_body(const std::string &src) {
  std::istringstream iss(src);
  Lexer lex(iss);
  Parser p(lex);
  auto prog = p.parse_program();
  return std::move(prog->body);
}

} // namespace

TEST(Lexer, TracksLineAndColumn) {
  std::istringstream iss("let\n  x =\n42;\nx");
  Lexer lex(iss);
  auto t0 = lex.next();  // let
  EXPECT_EQ(t0.loc.line, 1);
  EXPECT_EQ(t0.loc.col, 1);
  auto t1 = lex.next();  // x, second line, third column
  EXPECT_EQ(t1.loc.line, 2);
  EXPECT_EQ(t1.loc.col, 3);
  lex.next();            // =
  auto t3 = lex.next();  // 42, third line
  EXPECT_EQ(t3.loc.line, 3);
  EXPECT_EQ(t3.loc.col, 1);
}

TEST(Lexer, CommentsDoNotDisturbTheLineCount) {
  std::istringstream iss("// a comment\n// another\n7");
  Lexer lex(iss);
  auto t = lex.next();
  EXPECT_EQ(t.loc.line, 3);
}

TEST(Parser, StampsSourceLocationOnNodes) {
  auto body = parse_body("let x = 5;\nx + 1");
  ASSERT_NE(body, nullptr);
  EXPECT_TRUE(body->loc.known());
  EXPECT_EQ(body->loc.line, 1);  // the `let` starts the expression
}

TEST(Parser, ParseErrorCarriesThePositionOfTheOffendingToken) {
  std::istringstream iss("let x = 5;\nx +");
  Lexer lex(iss);
  Parser p(lex);
  try {
    p.parse_program();
    FAIL() << "expected a ParseError";
  } catch (const ParseError &e) {
    EXPECT_TRUE(e.loc.known());
    EXPECT_EQ(e.loc.line, 2);  // EOF, just past the trailing `+`
  }
}

TEST(TypeChecker, TypeErrorCarriesASourceLocation) {
  auto body = parse_body("let x = 5;\nlet y = true;\nx + y");
  ASSERT_NE(body, nullptr);
  Program prog(std::move(body));
  try {
    type_check(prog);
    FAIL() << "expected a TypeError";
  } catch (const TypeError &e) {
    EXPECT_TRUE(e.loc.known());
    EXPECT_EQ(e.loc.line, 3);  // the offending `x + y`
  }
}
