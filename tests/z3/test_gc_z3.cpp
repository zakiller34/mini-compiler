#include <gtest/gtest.h>
#include <z3.h>

#include <string>

// Phase 5 heap-object *header* invariants, proved over 64-bit bit-vectors.
//
// `test_tagging_z3.cpp` covers the Phase 8 *value* tags — the three low bits of
// a runtime value. This file covers the other tag: the word at offset 0 of
// every heap object, which the collector reads to learn an object's length and
// which of its slots are pointers.
//
// Layout (runtime/runtime.c):
//   bit 0     forwarding flag
//   bits 1-6  length (max 63 elements)
//   bits 7+   pointer mask, one bit per element
//
// The properties below are what `tag_length`, `tag_is_ptr` and `tag_is_fwd`
// depend on. A unit test can only sample them; these hold for every bit
// pattern.

namespace {

constexpr unsigned kBits = 64;
constexpr int64_t kLenShift = 1;
constexpr int64_t kLenMask = 0x3F;
constexpr int64_t kPtrShift = 7;
constexpr int64_t kMaxLen = 63;

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
    bool unsat() const { return Z3_solver_check(ctx, solver) == Z3_L_FALSE; }

    /// `tag_length(tag)` = (tag >> 1) & 0x3F
    Z3_ast tag_length(Z3_ast tag) const {
        return Z3_mk_bvand(ctx, Z3_mk_bvlshr(ctx, tag, bv(kLenShift)),
                           bv(kLenMask));
    }
    /// `tag_is_fwd(tag)` = tag & 1
    Z3_ast tag_is_fwd(Z3_ast tag) const {
        return Z3_mk_bvand(ctx, tag, bv(1));
    }
};

} // namespace

// Each property is asserted negated; UNSAT proves it.

/// @brief A tag built from a length recovers that length
/// ∀ len ∈ [0, 63]: tag_length(len << 1) == len
TEST(GCZ3, LengthFieldRoundTrips) {
    Ctx c;
    Z3_ast len = c.var("len");
    c.assert_ast(Z3_mk_bvule(c.ctx, c.bv(0), len));
    c.assert_ast(Z3_mk_bvule(c.ctx, len, c.bv(kMaxLen)));

    Z3_ast tag = Z3_mk_bvshl(c.ctx, len, c.bv(kLenShift));
    c.assert_ast(Z3_mk_not(c.ctx, Z3_mk_eq(c.ctx, c.tag_length(tag), len)));
    EXPECT_TRUE(c.unsat());
}

/// @brief The pointer mask never disturbs the length field
/// ∀ len ∈ [0,63], mask: tag_length((len << 1) | (mask << 7)) == len
TEST(GCZ3, PointerMaskDoesNotCorruptLength) {
    Ctx c;
    Z3_ast len = c.var("len");
    Z3_ast mask = c.var("mask");
    c.assert_ast(Z3_mk_bvule(c.ctx, c.bv(0), len));
    c.assert_ast(Z3_mk_bvule(c.ctx, len, c.bv(kMaxLen)));

    Z3_ast tag = Z3_mk_bvor(c.ctx, Z3_mk_bvshl(c.ctx, len, c.bv(kLenShift)),
                            Z3_mk_bvshl(c.ctx, mask, c.bv(kPtrShift)));
    c.assert_ast(Z3_mk_not(c.ctx, Z3_mk_eq(c.ctx, c.tag_length(tag), len)));
    EXPECT_TRUE(c.unsat());
}

/// @brief A well-formed tag is never mistaken for a forwarding pointer
/// ∀ len, mask: tag_is_fwd((len << 1) | (mask << 7)) == 0
///
/// This is why the length starts at bit 1 rather than bit 0. If it did not, a
/// live object of odd length would read as already-forwarded and the collector
/// would follow its length field as an address.
TEST(GCZ3, LiveTagIsNeverForwarded) {
    Ctx c;
    Z3_ast len = c.var("len");
    Z3_ast mask = c.var("mask");

    Z3_ast tag = Z3_mk_bvor(c.ctx, Z3_mk_bvshl(c.ctx, len, c.bv(kLenShift)),
                            Z3_mk_bvshl(c.ctx, mask, c.bv(kPtrShift)));
    c.assert_ast(Z3_mk_not(c.ctx, Z3_mk_eq(c.ctx, c.tag_is_fwd(tag), c.bv(0))));
    EXPECT_TRUE(c.unsat());
}

/// @brief A forwarding pointer round-trips
/// ∀ p: p & 1 == 0 ⇒ (p | 1) & ~1 == p
///
/// `copy_tuple` overwrites the tag with `dest | 1` and later recovers `dest`
/// as `tag & ~1`. That is only lossless because every heap address is
/// 8-byte aligned, hence even.
TEST(GCZ3, ForwardingPointerRoundTrips) {
    Ctx c;
    Z3_ast p = c.var("p");
    c.assert_ast(Z3_mk_eq(c.ctx, Z3_mk_bvand(c.ctx, p, c.bv(7)), c.bv(0)));

    Z3_ast fwd = Z3_mk_bvor(c.ctx, p, c.bv(1));
    c.assert_ast(Z3_mk_not(c.ctx, Z3_mk_eq(c.ctx, c.tag_is_fwd(fwd), c.bv(1))));
    EXPECT_TRUE(c.unsat());

    Ctx c2;
    Z3_ast q = c2.var("q");
    c2.assert_ast(Z3_mk_eq(c2.ctx, Z3_mk_bvand(c2.ctx, q, c2.bv(7)), c2.bv(0)));
    Z3_ast back = Z3_mk_bvand(c2.ctx, Z3_mk_bvor(c2.ctx, q, c2.bv(1)),
                              c2.bv(~int64_t{1}));
    c2.assert_ast(Z3_mk_not(c2.ctx, Z3_mk_eq(c2.ctx, back, q)));
    EXPECT_TRUE(c2.unsat());
}

/// @brief Every element index below the length has its own mask bit
/// ∀ i, j < 63, i ≠ j: setting bit i of the mask leaves bit j clear
///
/// The collector decides whether to trace slot i by reading bit 7+i. If two
/// indices shared a bit, a scalar slot would be traced as a pointer.
TEST(GCZ3, PointerMaskBitsAreIndependent) {
    Ctx c;
    Z3_ast i = c.var("i");
    Z3_ast j = c.var("j");
    c.assert_ast(Z3_mk_bvult(c.ctx, i, c.bv(kMaxLen)));
    c.assert_ast(Z3_mk_bvult(c.ctx, j, c.bv(kMaxLen)));
    c.assert_ast(Z3_mk_not(c.ctx, Z3_mk_eq(c.ctx, i, j)));

    // tag with only element i marked as a pointer
    Z3_ast tag = Z3_mk_bvshl(c.ctx, c.bv(1),
                             Z3_mk_bvadd(c.ctx, c.bv(kPtrShift), i));
    // tag_is_ptr(tag, j) = (tag >> (7 + j)) & 1
    Z3_ast bit_j = Z3_mk_bvand(
        c.ctx,
        Z3_mk_bvlshr(c.ctx, tag, Z3_mk_bvadd(c.ctx, c.bv(kPtrShift), j)),
        c.bv(1));
    c.assert_ast(Z3_mk_not(c.ctx, Z3_mk_eq(c.ctx, bit_j, c.bv(0))));
    EXPECT_TRUE(c.unsat());
}

/// @brief A 63-element object still fits the header
/// The length field is 6 bits, so 63 is the largest representable length and
/// `expose_allocation` must never emit a longer tuple.
TEST(GCZ3, MaxLengthFitsTheField) {
    Ctx c;
    Z3_ast tag = Z3_mk_bvshl(c.ctx, c.bv(kMaxLen), c.bv(kLenShift));
    c.assert_ast(
        Z3_mk_not(c.ctx, Z3_mk_eq(c.ctx, c.tag_length(tag), c.bv(kMaxLen))));
    EXPECT_TRUE(c.unsat());
}
