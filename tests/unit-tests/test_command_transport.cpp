#include "CppUTest/TestHarness.h"

extern "C" {
#include "command_transport.h"
#include "uart_spy.h"
}

TEST_GROUP(CommandTransport)
{
    void setup() override
    {
        UartSpy_Reset();
        command_transport_deinit();
    }
};

/* --- command_transport_init --- */

// init() should call uart_init() with the instance it was given.
TEST(CommandTransport, InitCallsUartInitWithGivenInstance)
{
    LONGS_EQUAL(STATUS_OK, command_transport_init(UART_INSTANCE_USART2));
    LONGS_EQUAL(UART_INSTANCE_USART2, UartSpy_GetLastInitInstance());
}

// init() should propagate uart_init()'s failure status without swallowing it.
TEST(CommandTransport, InitPropagatesUartInitFailure)
{
    UartSpy_SetReturnStatus(STATUS_ERR_INVALID_ARG);
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, command_transport_init(UART_INSTANCE_USART2));
}

/* --- command_transport_deinit --- */

// deinit() should be a no-op returning STATUS_OK when never initialised.
TEST(CommandTransport, DeinitIsNoOpWhenNeverInitialised)
{
    LONGS_EQUAL(STATUS_OK, command_transport_deinit());
}

// deinit() should call uart_deinit() with the instance stored at init, when previously initialised.
TEST(CommandTransport, DeinitCallsUartDeinitWithStoredInstance)
{
    command_transport_init(UART_INSTANCE_USART2);

    LONGS_EQUAL(STATUS_OK, command_transport_deinit());
    LONGS_EQUAL(1, UartSpy_GetDeinitCallCount());
    LONGS_EQUAL(UART_INSTANCE_USART2, UartSpy_GetLastDeinitInstance());
}

// deinit() should propagate uart_deinit()'s failure status without swallowing it.
TEST(CommandTransport, DeinitPropagatesUartDeinitFailure)
{
    command_transport_init(UART_INSTANCE_USART2);
    UartSpy_SetReturnStatus(STATUS_ERR_INVALID_ARG);

    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, command_transport_deinit());
}

/* --- command_transport_receive --- */

// receive() should reject calls made before init() (or after deinit()).
TEST(CommandTransport, ReceiveFailsWhenNotInitialised)
{
    uint8_t data = 0;
    LONGS_EQUAL(STATUS_ERR_NOT_INIT, command_transport_receive(&data));
}

// receive() should reject a NULL data pointer once initialised.
TEST(CommandTransport, ReceiveFailsWithNullData)
{
    command_transport_init(UART_INSTANCE_USART2);
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, command_transport_receive(NULL));
}

// receive() should call uart_read_byte() using the instance stored at init.
TEST(CommandTransport, ReceiveCallsUartReadByteWithStoredInstance)
{
    command_transport_init(UART_INSTANCE_USART2);

    uint8_t data = 0;
    command_transport_receive(&data);

    LONGS_EQUAL(UART_INSTANCE_USART2, UartSpy_GetLastReadByteInstance());
}

// receive() should return STATUS_OK and the byte when one is available.
TEST(CommandTransport, ReceiveReturnsByteWhenAvailable)
{
    command_transport_init(UART_INSTANCE_USART2);
    UartSpy_SetNextReadByte(0x42);

    uint8_t data = 0;
    LONGS_EQUAL(STATUS_OK, command_transport_receive(&data));
    LONGS_EQUAL(0x42, data);
}

// receive() should return STATUS_ERR_EMPTY when nothing's available.
TEST(CommandTransport, ReceiveReturnsEmptyWhenNothingAvailable)
{
    command_transport_init(UART_INSTANCE_USART2);

    uint8_t data = 0;
    LONGS_EQUAL(STATUS_ERR_EMPTY, command_transport_receive(&data));
}

/* --- command_transport_send_response --- */

// send_response() should reject calls made before init() (or after deinit()).
TEST(CommandTransport, SendResponseFailsWhenNotInitialised)
{
    const uint8_t frame[] = {0xA5, 0x01, 0x01, 0x1F, 0x3E};
    LONGS_EQUAL(STATUS_ERR_NOT_INIT, command_transport_send_response(frame, sizeof(frame)));
}

// send_response() should reject a NULL frame pointer once initialised.
TEST(CommandTransport, SendResponseFailsWithNullFrame)
{
    command_transport_init(UART_INSTANCE_USART2);
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, command_transport_send_response(NULL, 5));
}

// send_response() should write exactly the bytes it's given, unmodified - it doesn't
// build the frame itself, that's frame_parser's job.
TEST(CommandTransport, SendResponseWritesGivenBytes)
{
    command_transport_init(UART_INSTANCE_USART2);

    const uint8_t frame[] = {0xA5, 0x01, 0x01, 0x1F, 0x3E};
    LONGS_EQUAL(STATUS_OK, command_transport_send_response(frame, sizeof(frame)));

    LONGS_EQUAL(sizeof(frame), UartSpy_GetSentByteCount());
    for (uint16_t i = 0; i < sizeof(frame); i++)
    {
        BYTES_EQUAL(frame[i], UartSpy_GetSentByte(i));
    }
}

// send_response() should propagate a UART write failure without swallowing it.
TEST(CommandTransport, SendResponsePropagatesUartWriteFailure)
{
    command_transport_init(UART_INSTANCE_USART2);
    UartSpy_SetReturnStatus(STATUS_ERR_INVALID_ARG);

    const uint8_t frame[] = {0xA5, 0x01, 0x01, 0x1F, 0x3E};
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, command_transport_send_response(frame, sizeof(frame)));
}
