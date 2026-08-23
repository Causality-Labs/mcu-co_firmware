#include <cstring>
#include "CppUTest/TestHarness.h"

extern "C" {
#include "logger.h"
#include "uart_spy.h"
}

/* logger_initialized is a private static with no reset hook, so it can only
 * ever be set once for the life of this whole test binary. setup() calls
 * logger_init() every time: the first call really initialises it, every
 * call after that just hits the "already initialised" guard and no-ops
 * harmlessly, leaving logger_initialized true throughout. That means
 * "logger_log()/logger_flush() before init" and "the very first init call
 * succeeds" aren't independently testable here without adding a reset hook
 * to logger.c - not attempted below. */
TEST_GROUP(Logger)
{
    void setup() override
    {
        UartSpy_Reset();
        (void)logger_init();
        logger_flush(); // drain anything left queued by a previous test
        UartSpy_Reset(); // discard whatever the drain above emitted
    }
};

static void CheckEmittedBytesEqual(const char *expected)
{
    size_t len = strlen(expected);
    LONGS_EQUAL((long)len, UartSpy_GetSentByteCount());
    for (size_t i = 0; i < len; i++)
    {
        BYTES_EQUAL((uint8_t)expected[i], UartSpy_GetSentByte((uint16_t)i));
    }
}

/* --- logger_init --- */

// Calling init again once already initialised should fail rather than
// silently re-initialising (which would leak/reset the queue mid-run).
TEST(Logger, LoggerInitReturnsNegativeOneWhenAlreadyInitialized)
{
    LONGS_EQUAL(-1, logger_init());
}

/* --- logger_log --- */

// A NULL module string should be ignored - nothing enqueued, nothing
// emitted on the next flush.
TEST(Logger, LogIgnoresNullModule)
{
    logger_log(LOG_LEVEL_ERROR, NULL, 0U, "msg");
    logger_flush();
    LONGS_EQUAL(0, UartSpy_GetSentByteCount());
}

// A NULL message string should be ignored the same way.
TEST(Logger, LogIgnoresNullMessage)
{
    logger_log(LOG_LEVEL_ERROR, "MOD", 0U, NULL);
    logger_flush();
    LONGS_EQUAL(0, UartSpy_GetSentByteCount());
}

// Arguments are copied when the entry is queued, not read when it is
// flushed: changing the variable after the log call must not change what
// gets emitted. This is the whole point of storing argument words rather
// than formatting later from the caller's storage.
TEST(Logger, LogCapturesArgumentValueAtLogTime)
{
    uint32_t counter = 7U;
    logger_log(LOG_LEVEL_INFO, "MOD", 1U, "count %u", counter);

    counter = 99U;
    logger_flush();

    CheckEmittedBytesEqual("[INFO] MOD: count 7\r\n");
}

/* --- logger_flush --- */

// A queued entry should be emitted as "[LEVEL] module: message\r\n".
TEST(Logger, FlushEmitsQueuedEntryInBracketedFormat)
{
    logger_log(LOG_LEVEL_ERROR, "MOD", 0U, "hello");
    logger_flush();
    CheckEmittedBytesEqual("[ERROR] MOD: hello\r\n");
}

// Each severity level must map to its own label - a mismatch here would
// mean a copy-paste error in the level-to-string lookup.
TEST(Logger, FlushEmitsCorrectLabelForEachLevel)
{
    logger_log(LOG_LEVEL_ERROR, "M", 0U, "e");
    logger_flush();
    CheckEmittedBytesEqual("[ERROR] M: e\r\n");
    UartSpy_Reset();

    logger_log(LOG_LEVEL_WARN, "M", 0U, "w");
    logger_flush();
    CheckEmittedBytesEqual("[WARN] M: w\r\n");
    UartSpy_Reset();

    logger_log(LOG_LEVEL_INFO, "M", 0U, "i");
    logger_flush();
    CheckEmittedBytesEqual("[INFO] M: i\r\n");
    UartSpy_Reset();

    logger_log(LOG_LEVEL_DEBUG, "M", 0U, "d");
    logger_flush();
    CheckEmittedBytesEqual("[DEBUG] M: d\r\n");
}

// Multiple queued entries should be emitted in FIFO order, back to back.
TEST(Logger, FlushEmitsMultipleQueuedEntriesInOrder)
{
    logger_log(LOG_LEVEL_ERROR, "A", 0U, "first");
    logger_log(LOG_LEVEL_INFO, "B", 0U, "second");
    logger_flush();

    CheckEmittedBytesEqual("[ERROR] A: first\r\n[INFO] B: second\r\n");
}

// After a flush drains the queue, a second flush with nothing newly queued
// should emit nothing.
TEST(Logger, FlushDrainsQueueCompletely)
{
    logger_log(LOG_LEVEL_ERROR, "MOD", 0U, "hi");
    logger_flush();
    CHECK_TRUE(UartSpy_GetSentByteCount() > 0);

    UartSpy_Reset();
    logger_flush();
    LONGS_EQUAL(0, UartSpy_GetSentByteCount());
}
