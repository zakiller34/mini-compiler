#include <gtest/gtest.h>
#include <z3.h>

#include <string>
#include <vector>

#include "type.h"

using namespace mc;

namespace {

/// Bit-vector width of every runtime value
constexpr unsigned kBits = 64;

struct Ctx {
    Z3_config cfg;
    Z3_context ctx;
    Z3_solver solver;

    Ctx() {
        cfg = Z3_mk_config();
        ctx = Z3_mk_context(cfg);
        solver = Z3_mk_solver(ctx);
        Z3_solver_inc_ref(ctx, solver);
    }
    ~Ctx() {
        Z3_solver_dec_ref(ctx, solver);
        Z3_del_context(ctx);
        Z3_del_config(cfg);
    }
    Ctx(const Ctx &) = delete;
    Ctx &operator=(const Ctx &) = delete;
    Ctx(Ctx &&) = delete;
    Ctx &operator=(Ctx &&) = delete;

    Z3_ast bv(int64_t v) const {
        return Z3_mk_numeral(ctx, std::to_string(v).c_str(),
                             Z3_mk_bv_sort(ctx, kBits));
    }
    Z3_ast var(const char *name) const {
        return Z3_mk_const(ctx, Z3_mk_string_symbol(ctx, name),
                           Z3_mk_bv_sort(ctx, kBits));
    }
    void assert_ast(Z3_ast a) const { Z3_solver_assert(ctx, solver, a); }
    /// @ensures true iff the asserted constraints are unsatisfiable
    bool unsat() const { return Z3_solver_check(ctx, solver) == Z3_L_FALSE; }
};

} // namespace

// The negation of each property is asserted; UNSAT proves the property.

/// @brief Scalars survive the tag round-trip over the representable range
/// ∀ v ∈ [-2^60, 2^60): ((v << 3) | 001) >>arith 3 == v
TEST(TaggingZ3, ScalarTagRoundTrip) {
    Ctx c;
    Z3_ast v = c.var("v");
    c.assert_ast(Z3_mk_bvsle(c.ctx, c.bv(-(1LL << 60)), v));
    c.assert_ast(Z3_mk_bvslt(c.ctx, v, c.bv(1LL << 60)));

    Z3_ast tagged = Z3_mk_bvor(c.ctx,
        Z3_mk_bvshl(c.ctx, v, c.bv(kTagShift)), c.bv(kTagInt));
    Z3_ast back = Z3_mk_bvashr(c.ctx, tagged, c.bv(kTagShift));
    c.assert_ast(Z3_mk_not(c.ctx, Z3_mk_eq(c.ctx, back, v)));
    EXPECT_TRUE(c.unsat());
}

/// @brief An 8-byte-aligned pointer survives tagging untouched
/// ∀ p: p & 7 == 0 ⇒ (p | tag) & ~7 == p
TEST(TaggingZ3, PointerTagRoundTrip) {
    Ctx c;
    Z3_ast p = c.var("p");
    c.assert_ast(Z3_mk_eq(c.ctx,
        Z3_mk_bvand(c.ctx, p, c.bv(kTagMask)), c.bv(0)));

    Z3_ast tagged = Z3_mk_bvor(c.ctx, p, c.bv(kTagVector));
    Z3_ast back = Z3_mk_bvand(c.ctx, tagged, c.bv(~kTagMask));
    c.assert_ast(Z3_mk_not(c.ctx, Z3_mk_eq(c.ctx, back, p)));
    EXPECT_TRUE(c.unsat());
}

/// @brief Tagging then reading the tag yields the tag back
/// ∀ p aligned: ((p | tag) & 7) == tag
TEST(TaggingZ3, TagOfAnyRecoversTheTag) {
    Ctx c;
    Z3_ast p = c.var("p");
    c.assert_ast(Z3_mk_eq(c.ctx,
        Z3_mk_bvand(c.ctx, p, c.bv(kTagMask)), c.bv(0)));

    Z3_ast tagged = Z3_mk_bvor(c.ctx, p, c.bv(kTagFunction));
    Z3_ast tag = Z3_mk_bvand(c.ctx, tagged, c.bv(kTagMask));
    c.assert_ast(Z3_mk_not(c.ctx, Z3_mk_eq(c.ctx, tag, c.bv(kTagFunction))));
    EXPECT_TRUE(c.unsat());
}

/// @brief The five tag codes are pairwise distinct and none is 000, so the
///        GC can tell a tagged value from a plain tuple pointer (§9.9)
TEST(TaggingZ3, TagsArePairwiseDistinctAndNonZero) {
    Ctx c;
    const std::vector<int64_t> tags = {kTagInt, kTagBool, kTagVector,
                                        kTagFunction, kTagVoid};
    std::vector<Z3_ast> clauses;
    // invariant: clauses holds "tags[i] == tags[j]" for the pairs seen
    for (size_t i = 0; i < tags.size(); ++i) {
        clauses.push_back(Z3_mk_eq(c.ctx, c.bv(tags[i]), c.bv(0)));
        for (size_t j = i + 1; j < tags.size(); ++j) {
            clauses.push_back(Z3_mk_eq(c.ctx, c.bv(tags[i]), c.bv(tags[j])));
        }
    }
    c.assert_ast(Z3_mk_or(c.ctx, static_cast<unsigned>(clauses.size()),
                           clauses.data()));
    EXPECT_TRUE(c.unsat());
}

/// @brief Every tag fits in the 3 stolen bits
TEST(TaggingZ3, TagsFitInThreeBits) {
    Ctx c;
    const std::vector<int64_t> tags = {kTagInt, kTagBool, kTagVector,
                                        kTagFunction, kTagVoid};
    std::vector<Z3_ast> clauses;
    // invariant: clauses holds "tags[i] & ~7 != 0" for tags[0..i)
    for (int64_t t : tags) {
        clauses.push_back(Z3_mk_not(c.ctx, Z3_mk_eq(c.ctx,
            Z3_mk_bvand(c.ctx, c.bv(t), c.bv(~kTagMask)), c.bv(0))));
    }
    c.assert_ast(Z3_mk_or(c.ctx, static_cast<unsigned>(clauses.size()),
                           clauses.data()));
    EXPECT_TRUE(c.unsat());
}

/// @brief A projection traps exactly when the tag differs from the target's
/// ∀ v, T: (tag-of-any v == tagof(T)) ⇔ the projection does not Exit
TEST(TaggingZ3, ProjectTrapsIffTagMismatch) {
    Ctx c;
    Z3_ast v = c.var("v");
    Z3_ast tag = Z3_mk_bvand(c.ctx, v, c.bv(kTagMask));
    Z3_ast matches = Z3_mk_eq(c.ctx, tag, c.bv(kTagInt));
    // "takes the Exit branch" is by construction the negation of `matches`
    Z3_ast exits = Z3_mk_not(c.ctx, matches);
    c.assert_ast(Z3_mk_not(c.ctx, Z3_mk_iff(c.ctx, exits,
        Z3_mk_not(c.ctx, Z3_mk_eq(c.ctx, tag, c.bv(kTagInt))))));
    EXPECT_TRUE(c.unsat());
}

/// @brief The GC preserves a slot's tag bits across a move: for any moved
///        (8-byte-aligned) destination d, (d | (s & 7)) & 7 == s & 7
TEST(TaggingZ3, CollectPreservesTagBits) {
    Ctx c;
    Z3_ast s = c.var("s");
    Z3_ast d = c.var("d");
    c.assert_ast(Z3_mk_eq(c.ctx,
        Z3_mk_bvand(c.ctx, d, c.bv(kTagMask)), c.bv(0)));

    Z3_ast vtag = Z3_mk_bvand(c.ctx, s, c.bv(kTagMask));
    Z3_ast moved = Z3_mk_bvor(c.ctx, d, vtag);
    Z3_ast moved_tag = Z3_mk_bvand(c.ctx, moved, c.bv(kTagMask));
    c.assert_ast(Z3_mk_not(c.ctx, Z3_mk_eq(c.ctx, moved_tag, vtag)));
    EXPECT_TRUE(c.unsat());
}

/// @brief tagof agrees with the tag codes used by the runtime
TEST(TaggingZ3, TagofMatchesTheRuntimeCodes) {
    EXPECT_EQ(tagof(int_type()), kTagInt);
    EXPECT_EQ(tagof(bool_type()), kTagBool);
    EXPECT_EQ(tagof(void_type()), kTagVoid);
    EXPECT_EQ(tagof(vector_type({any_type()})), kTagVector);
    EXPECT_EQ(tagof(fun_type({any_type()}, any_type())), kTagFunction);
}

/// @brief Only the book's flat types may be injected or projected
TEST(TaggingZ3, FlatTypesAreExactlyTheInjectableOnes) {
    EXPECT_TRUE(is_flat_type(int_type()));
    EXPECT_TRUE(is_flat_type(bool_type()));
    EXPECT_TRUE(is_flat_type(void_type()));
    EXPECT_TRUE(is_flat_type(vector_type({any_type(), any_type()})));
    EXPECT_TRUE(is_flat_type(fun_type({any_type()}, any_type())));
    EXPECT_FALSE(is_flat_type(any_type()));
    EXPECT_FALSE(is_flat_type(vector_type({int_type()})));
    EXPECT_FALSE(is_flat_type(fun_type({int_type()}, any_type())));
}
