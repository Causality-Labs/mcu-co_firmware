#include "CppUTest/TestHarness.h"

extern "C" {
#include "crc16.h"
}

TEST_GROUP(Crc16){};

/* --- crc16_compute --- */

// A NULL buffer should return the initial CRC value untouched, regardless of
// the size argument.
TEST(Crc16, ComputeReturnsInitOnNullBuffer)
{
    LONGS_EQUAL(0xFFFF, crc16_compute(NULL, 10));
}

// A zero-length buffer means the accumulation loop never runs, so the result
// should still be the initial CRC value.
TEST(Crc16, ComputeReturnsInitOnZeroSize)
{
    uint8_t data[1] = {0xAB};
    LONGS_EQUAL(0xFFFF, crc16_compute(data, 0));
}

// Cross-checks the implementation against the published CRC-16/CCITT-FALSE
// catalog test vector for "123456789" (init 0xFFFF, poly 0x1021, no
// reflection) - this is the test that actually proves the CRC math is
// correct, not just that the guard clauses work.
TEST(Crc16, ComputeMatchesKnownVector)
{
    const uint8_t data[] = "123456789";
    LONGS_EQUAL(0x29B1, crc16_compute(data, 9));
}

/* --- crc16_compare --- */

// Equal CRC values should compare as a match.
TEST(Crc16, CompareTrueWhenEqual)
{
    CHECK_TRUE(crc16_compare(0x1234, 0x1234));
}

// Different CRC values should compare as a mismatch.
TEST(Crc16, CompareFalseWhenDifferent)
{
    CHECK_FALSE(crc16_compare(0x1234, 0x5678));
}
