#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

// The collector is C. Phase 5 shipped it with no direct tests at all — it was
// exercised only end-to-end, through generated assembly, where a wrong answer
// is nearly impossible to attribute. These tests drive it directly.
extern "C" {
void initialize(int64_t rootstack_size, int64_t heap_size);
void collect(int64_t *rootstack_ptr, int64_t bytes);
extern int64_t *free_ptr;
extern int64_t *fromspace_begin;
extern int64_t *fromspace_end;
extern int64_t *tospace_begin;
extern int64_t *tospace_end;
extern int64_t *rootstack_begin;
}

namespace {

constexpr int64_t kHeapBytes = 1024;
constexpr int64_t kRootBytes = 256;

/// Tag layout (runtime.c): bit 0 forwarding, bits 1-6 length,
/// bits 7+ pointer mask.
int64_t make_tag(int64_t len, std::vector<bool> is_ptr) {
    int64_t tag = len << 1;
    for (size_t i = 0; i < is_ptr.size(); ++i) {
        if (is_ptr[i]) tag |= (int64_t{1} << (7 + i));
    }
    return tag;
}

/// Bump-allocate a tuple in fromspace and return it.
int64_t *alloc_tuple(int64_t len, const std::vector<bool> &is_ptr) {
    int64_t *t = free_ptr;
    t[0] = make_tag(len, is_ptr);
    for (int64_t i = 0; i < len; ++i) t[i + 1] = 0;
    free_ptr += len + 1;
    return t;
}

/// Fresh heap before each test; the collector's state is global.
class GCTest : public ::testing::Test {
  protected:
    void SetUp() override { initialize(kRootBytes, kHeapBytes); }
};

} // namespace

TEST_F(GCTest, InitializeEstablishesTheHeapInvariants) {
    EXPECT_NE(fromspace_begin, nullptr);
    EXPECT_NE(tospace_begin, nullptr);
    EXPECT_NE(rootstack_begin, nullptr);
    EXPECT_EQ(free_ptr, fromspace_begin);
    EXPECT_EQ(fromspace_end - fromspace_begin, kHeapBytes / 8);
    EXPECT_EQ(tospace_end - tospace_begin, kHeapBytes / 8);
    // The two semispaces must not overlap, or copying corrupts the source.
    EXPECT_TRUE(fromspace_end <= tospace_begin ||
                tospace_end <= fromspace_begin);
}

TEST_F(GCTest, RootedTupleSurvivesAndIsRelocated) {
    int64_t *t = alloc_tuple(2, {false, false});
    t[1] = 111;
    t[2] = 222;
    int64_t *old_from = fromspace_begin;

    rootstack_begin[0] = reinterpret_cast<int64_t>(t);
    collect(rootstack_begin + 1, 0);

    // Spaces swapped, so the survivor now lives in the new fromspace.
    EXPECT_NE(fromspace_begin, old_from);
    auto *moved = reinterpret_cast<int64_t *>(rootstack_begin[0]);
    ASSERT_NE(moved, nullptr);
    EXPECT_GE(moved, fromspace_begin);
    EXPECT_LT(moved, fromspace_end);
    EXPECT_EQ(moved[1], 111);
    EXPECT_EQ(moved[2], 222);
    // Exactly one tuple of 3 words survived.
    EXPECT_EQ(free_ptr - fromspace_begin, 3);
}

TEST_F(GCTest, UnrootedTupleIsReclaimed) {
    int64_t *live = alloc_tuple(1, {false});
    live[1] = 7;
    alloc_tuple(4, {false, false, false, false}); // garbage: never rooted

    rootstack_begin[0] = reinterpret_cast<int64_t>(live);
    collect(rootstack_begin + 1, 0);

    // Only the 2-word live tuple should have been copied.
    EXPECT_EQ(free_ptr - fromspace_begin, 2);
}

TEST_F(GCTest, NestedTuplesAreTracedTransitively) {
    int64_t *child = alloc_tuple(1, {false});
    child[1] = 99;
    int64_t *parent = alloc_tuple(1, {true}); // element 0 is a pointer
    parent[1] = reinterpret_cast<int64_t>(child);

    rootstack_begin[0] = reinterpret_cast<int64_t>(parent);
    collect(rootstack_begin + 1, 0);

    auto *p = reinterpret_cast<int64_t *>(rootstack_begin[0]);
    auto *c = reinterpret_cast<int64_t *>(p[1]);
    ASSERT_NE(c, nullptr);
    EXPECT_GE(c, fromspace_begin);
    EXPECT_LT(c, fromspace_end);
    EXPECT_EQ(c[1], 99);
    // Parent (2 words) + child (2 words).
    EXPECT_EQ(free_ptr - fromspace_begin, 4);
}

TEST_F(GCTest, SharedChildIsCopiedOnceViaForwardingPointer) {
    int64_t *shared = alloc_tuple(1, {false});
    shared[1] = 5;
    int64_t *a = alloc_tuple(1, {true});
    a[1] = reinterpret_cast<int64_t>(shared);
    int64_t *b = alloc_tuple(1, {true});
    b[1] = reinterpret_cast<int64_t>(shared);

    rootstack_begin[0] = reinterpret_cast<int64_t>(a);
    rootstack_begin[1] = reinterpret_cast<int64_t>(b);
    collect(rootstack_begin + 2, 0);

    auto *a2 = reinterpret_cast<int64_t *>(rootstack_begin[0]);
    auto *b2 = reinterpret_cast<int64_t *>(rootstack_begin[1]);
    // Aliasing must be preserved: one copy, two references to it.
    EXPECT_EQ(a2[1], b2[1]);
    // a (2) + shared (2) + b (2) = 6 words, not 8.
    EXPECT_EQ(free_ptr - fromspace_begin, 6);
}

TEST_F(GCTest, CyclicStructureTerminatesAndPreservesTheCycle) {
    int64_t *x = alloc_tuple(1, {true});
    int64_t *y = alloc_tuple(1, {true});
    x[1] = reinterpret_cast<int64_t>(y);
    y[1] = reinterpret_cast<int64_t>(x);

    rootstack_begin[0] = reinterpret_cast<int64_t>(x);
    collect(rootstack_begin + 1, 0); // must not diverge

    auto *x2 = reinterpret_cast<int64_t *>(rootstack_begin[0]);
    auto *y2 = reinterpret_cast<int64_t *>(x2[1]);
    EXPECT_EQ(reinterpret_cast<int64_t *>(y2[1]), x2);
    EXPECT_EQ(free_ptr - fromspace_begin, 4);
}

TEST_F(GCTest, NonPointerSlotsAreNeverFollowed) {
    // A slot holding an integer that happens to look like a heap address must
    // not be traced, because the tag's pointer mask says it is not a pointer.
    int64_t *t = alloc_tuple(1, {false});
    const auto decoy = reinterpret_cast<int64_t>(fromspace_begin);
    t[1] = decoy; // a plain integer that is also a valid heap address

    rootstack_begin[0] = reinterpret_cast<int64_t>(t);
    collect(rootstack_begin + 1, 0);

    auto *moved = reinterpret_cast<int64_t *>(rootstack_begin[0]);
    // Copied verbatim, and nothing extra was traced.
    EXPECT_EQ(moved[1], decoy);
    EXPECT_EQ(free_ptr - fromspace_begin, 2);
}

TEST_F(GCTest, TaggedImmediateRootsAreSkipped) {
    // Phase 8: a root slot may hold a tagged Any value rather than a pointer.
    // Tags 001 (int), 100 (bool) and 101 (void) carry immediate data, so the
    // collector must leave them alone instead of dereferencing them.
    constexpr int64_t kTagInt = 1;
    constexpr int64_t kTagBool = 4;
    const int64_t tagged_int = (int64_t{42} << 3) | kTagInt;
    const int64_t tagged_bool = (int64_t{1} << 3) | kTagBool;

    rootstack_begin[0] = tagged_int;
    rootstack_begin[1] = tagged_bool;
    collect(rootstack_begin + 2, 0);

    EXPECT_EQ(rootstack_begin[0], tagged_int);
    EXPECT_EQ(rootstack_begin[1], tagged_bool);
    EXPECT_EQ(free_ptr, fromspace_begin); // nothing copied
}

TEST_F(GCTest, CollectPreservesTagBitsOfRelocatedPointers) {
    // A tagged tuple pointer (tag 010) must still carry its tag after the
    // object moves, or the mutator stops recognising it as a vector.
    constexpr int64_t kTagVector = 2;
    int64_t *t = alloc_tuple(1, {false});
    t[1] = 13;

    rootstack_begin[0] = reinterpret_cast<int64_t>(t) | kTagVector;
    collect(rootstack_begin + 1, 0);

    EXPECT_EQ(rootstack_begin[0] & 7, kTagVector);
    auto *moved = reinterpret_cast<int64_t *>(rootstack_begin[0] & ~int64_t{7});
    EXPECT_EQ(moved[1], 13);
}

TEST_F(GCTest, RepeatedCollectionsKeepTheLiveSetStable) {
    int64_t *t = alloc_tuple(2, {false, false});
    t[1] = 1;
    t[2] = 2;
    rootstack_begin[0] = reinterpret_cast<int64_t>(t);

    // decreases 10 - i
    for (int i = 0; i < 10; ++i) {
        collect(rootstack_begin + 1, 0);
        auto *m = reinterpret_cast<int64_t *>(rootstack_begin[0]);
        ASSERT_GE(m, fromspace_begin);
        ASSERT_LT(m, fromspace_end);
        EXPECT_EQ(m[1], 1);
        EXPECT_EQ(m[2], 2);
        // Live set never grows: no duplication across collections.
        EXPECT_EQ(free_ptr - fromspace_begin, 3);
    }
}
