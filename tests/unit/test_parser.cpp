#include <gtest/gtest.h>

#include <memory>
#include <string>

#include <sstream>

#include "ast.h"
#include "lexer.h"
#include "parser.h"

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
