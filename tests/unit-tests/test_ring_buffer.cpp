#include "CppUTest/TestHarness.h"

extern "C" {
#include "ring-buffer.h"
}

#define CAPACITY 4U /* 1 slot reserved: holds at most CAPACITY - 1 elements */

TEST_GROUP(RingBuffer)
{
    uint8_t storage[CAPACITY];
    ring_buffer_t rb;

    void setup() override
    {
        ring_buffer_init(&rb, storage, CAPACITY, sizeof(uint8_t));
    }
};

TEST(RingBuffer, InitRejectsNullRb)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, ring_buffer_init(NULL, storage, CAPACITY, sizeof(uint8_t)));
}

TEST(RingBuffer, InitRejectsNullBuffer)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, ring_buffer_init(&rb, NULL, CAPACITY, sizeof(uint8_t)));
}

TEST(RingBuffer, InitRejectsZeroElementSize)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, ring_buffer_init(&rb, storage, CAPACITY, 0));
}

TEST(RingBuffer, InitRejectsZeroCapacity)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, ring_buffer_init(&rb, storage, 0, sizeof(uint8_t)));
}

TEST(RingBuffer, InitRejectsNonPowerOfTwoCapacity)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, ring_buffer_init(&rb, storage, 3, sizeof(uint8_t)));
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, ring_buffer_init(&rb, storage, 6, sizeof(uint8_t)));
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, ring_buffer_init(&rb, storage, 100, sizeof(uint8_t)));
}

TEST(RingBuffer, InitAcceptsValidArgsAndResultIsEmpty)
{
    LONGS_EQUAL(STATUS_OK, ring_buffer_init(&rb, storage, CAPACITY, sizeof(uint8_t)));
    CHECK_TRUE(ring_buffer_is_empty(&rb));
}

TEST(RingBuffer, WriteRejectsNullRb)
{
    uint8_t val = 1;
    bool overwrite = false;
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, ring_buffer_write(NULL, &val, overwrite));
}

TEST(RingBuffer, WriteRejectsNullElement)
{
    bool overwrite = false;
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, ring_buffer_write(&rb, NULL, overwrite));
}

TEST(RingBuffer, WriteStoresElementAtHead)
{
    uint8_t val = 42;
    bool overwrite = false;
    LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &val, overwrite));
    BYTES_EQUAL(42, storage[0]);
}

TEST(RingBuffer, WriteAdvancesHead)
{
    uint8_t val = 1;
    bool overwrite = false;

    LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &val, overwrite));
    LONGS_EQUAL(1, rb.head);

    LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &val, overwrite));
    LONGS_EQUAL(2, rb.head);
}

TEST(RingBuffer, WriteFailsWhenBufferIsFull)
{
    uint8_t oldest = 11;
    uint8_t val = 0;
    bool overwrite = false;

    LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &oldest, overwrite));
    for (uint8_t i = 1; i < CAPACITY - 1U; i++)
    {
        val = i;
        LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &val, overwrite));
    }

    CHECK_TRUE(ring_buffer_is_full(&rb));

    val = 99;
    LONGS_EQUAL(STATUS_ERR_FULL, ring_buffer_write(&rb, &val, overwrite));
    BYTES_EQUAL(11, storage[0]);
}

TEST(RingBuffer, WriteOverwritesOldestWhenFullAndOverwriteTrue)
{
    uint8_t a = 1;
    uint8_t b = 2;
    uint8_t c = 3;
    uint8_t d = 4;
    bool no_overwrite = false;
    bool overwrite    = true;

    LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &a, no_overwrite));
    LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &b, no_overwrite));
    LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &c, no_overwrite));
    CHECK_TRUE(ring_buffer_is_full(&rb));

    LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &d, overwrite));

    uint8_t out = 0;
    LONGS_EQUAL(STATUS_OK, ring_buffer_read(&rb, &out));
    BYTES_EQUAL(2, out);
    LONGS_EQUAL(STATUS_OK, ring_buffer_read(&rb, &out));
    BYTES_EQUAL(3, out);
    LONGS_EQUAL(STATUS_OK, ring_buffer_read(&rb, &out));
    BYTES_EQUAL(4, out);
    CHECK_TRUE(ring_buffer_is_empty(&rb));
}

TEST(RingBuffer, WriteWithOverwriteTrueOnNonFullBufferBehavesNormally)
{
    uint8_t val      = 7;
    bool overwrite   = true;

    LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &val, overwrite));
    BYTES_EQUAL(7, storage[0]);
    LONGS_EQUAL(1, rb.head);
    LONGS_EQUAL(0, rb.tail);
    CHECK_FALSE(ring_buffer_is_full(&rb));
}

TEST(RingBuffer, ReadRejectsNullRb)
{
    uint8_t out = 0;
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, ring_buffer_read(NULL, &out));
}

TEST(RingBuffer, ReadRejectsNullElement)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, ring_buffer_read(&rb, NULL));
}

TEST(RingBuffer, ReadFailsWhenBufferIsEmpty)
{
    uint8_t out = 0;
    LONGS_EQUAL(STATUS_ERR_EMPTY, ring_buffer_read(&rb, &out));
}

TEST(RingBuffer, ReadReturnsOldestElement)
{
    uint8_t a = 1;
    uint8_t b = 2;
    bool overwrite = false;

    LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &a, overwrite));
    LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &b, overwrite));

    uint8_t out = 0;
    LONGS_EQUAL(STATUS_OK, ring_buffer_read(&rb, &out));
    BYTES_EQUAL(1, out);
}

TEST(RingBuffer, ReadAdvancesTail)
{
    uint8_t val = 1;
    bool overwrite = false;
    uint8_t out = 0;

    LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &val, overwrite));
    LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &val, overwrite));

    LONGS_EQUAL(STATUS_OK, ring_buffer_read(&rb, &out));
    LONGS_EQUAL(1, rb.tail);

    LONGS_EQUAL(STATUS_OK, ring_buffer_read(&rb, &out));
    LONGS_EQUAL(2, rb.tail);
}

TEST(RingBuffer, FlushRejectsNull)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, ring_buffer_flush(NULL));
}

TEST(RingBuffer, FlushEmptiesBufferAndAllowsReuse)
{
    uint8_t val = 1;
    bool overwrite = false;

    LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &val, overwrite));
    LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &val, overwrite));

    LONGS_EQUAL(STATUS_OK, ring_buffer_flush(&rb));
    CHECK_TRUE(ring_buffer_is_empty(&rb));

    for (uint8_t i = 0; i < CAPACITY - 1U; i++)
    {
        LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &val, overwrite));
    }
    CHECK_TRUE(ring_buffer_is_full(&rb));
}

TEST(RingBuffer, IsEmptyFalseOnNull)
{
    CHECK_FALSE(ring_buffer_is_empty(NULL));
}

TEST(RingBuffer, IsEmptyTrueAfterAllWrittenElementsAreRead)
{
    uint8_t val = 1;
    bool overwrite = false;
    uint8_t out = 0;

    LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &val, overwrite));
    CHECK_FALSE(ring_buffer_is_empty(&rb));

    LONGS_EQUAL(STATUS_OK, ring_buffer_read(&rb, &out));
    CHECK_TRUE(ring_buffer_is_empty(&rb));
}

TEST(RingBuffer, IsFullFalseOnNull)
{
    CHECK_FALSE(ring_buffer_is_full(NULL));
}

TEST(RingBuffer, IsFullFalseAfterReadFreesSlot)
{
    uint8_t val = 1;
    bool overwrite = false;
    uint8_t out = 0;

    for (uint8_t i = 0; i < CAPACITY - 1U; i++)
    {
        LONGS_EQUAL(STATUS_OK, ring_buffer_write(&rb, &val, overwrite));
    }
    CHECK_TRUE(ring_buffer_is_full(&rb));

    LONGS_EQUAL(STATUS_OK, ring_buffer_read(&rb, &out));
    CHECK_FALSE(ring_buffer_is_full(&rb));
}
