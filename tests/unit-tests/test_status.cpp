#include "CppUTest/TestHarness.h"

extern "C" {
#include "status.h"
}

TEST_GROUP(Status){};

/* --- status_to_str --- */

// Every enum value should map to its documented string name - a mismatch
// here usually means a copy-paste error in the switch statement.
TEST(Status, StatusToStrMapsAllKnownValues)
{
    STRCMP_EQUAL("OK", status_to_str(STATUS_OK));
    STRCMP_EQUAL("ERR", status_to_str(STATUS_ERR));
    STRCMP_EQUAL("INVALID_ARG", status_to_str(STATUS_ERR_INVALID_ARG));
    STRCMP_EQUAL("INVALID_PIN", status_to_str(STATUS_ERR_INVALID_PIN));
    STRCMP_EQUAL("INVALID_STATE", status_to_str(STATUS_ERR_INVALID_STATE));
    STRCMP_EQUAL("NOT_INIT", status_to_str(STATUS_ERR_NOT_INIT));
    STRCMP_EQUAL("BUSY", status_to_str(STATUS_ERR_BUSY));
    STRCMP_EQUAL("TIMEOUT", status_to_str(STATUS_ERR_TIMEOUT));
    STRCMP_EQUAL("UNSUPPORTED", status_to_str(STATUS_ERR_UNSUPPORTED));
    STRCMP_EQUAL("EMPTY", status_to_str(STATUS_ERR_EMPTY));
    STRCMP_EQUAL("FULL", status_to_str(STATUS_ERR_FULL));
}

// A value outside the status_t enum should fall through to the default case
// and return "?" rather than reading garbage or crashing.
TEST(Status, StatusToStrReturnsQuestionMarkForUnknownValue)
{
    STRCMP_EQUAL("?", status_to_str((status_t)999));
}
