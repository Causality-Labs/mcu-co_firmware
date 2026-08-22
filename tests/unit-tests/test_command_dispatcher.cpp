#include <cstring>
#include "CppUTest/TestHarness.h"

extern "C" {
#include "command_dispatcher.h"
#include "gpio_controller_spy.h"
}

#define OPCODE_GPIO_CFG        0x30U
#define OPCODE_GPIO_WRITE      0x31U
#define OPCODE_GPIO_READ       0x32U
#define OPCODE_GPIO_IRQ_BIND   0x33U
#define OPCODE_GPIO_IRQ_CFG    0x34U
#define OPCODE_GPIO_IRQ_UNBIND 0x35U

TEST_GROUP(CommandDispatcher)
{
    frame_t frame;
    response_t resp;

    void setup() override
    {
        GpioControllerSpy_Reset();
        frame = frame_t();
        resp  = response_t();
    }
};

/* --- dispatch_command --- */

// A NULL frame pointer should be rejected.
TEST(CommandDispatcher, DispatchRejectsNullFrame)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, dispatch_command(NULL, &resp));
}

// A NULL response pointer should be rejected.
TEST(CommandDispatcher, DispatchRejectsNullResponse)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, dispatch_command(&frame, NULL));
}

// GPIO_CFG (0x30) should route to gpio_controller_io_cfg(), forwarding the
// frame's payload/length unchanged, and ack the response on success.
TEST(CommandDispatcher, DispatchRoutesGpioCfgOpcodeAndForwardsPayload)
{
    frame.opcode      = OPCODE_GPIO_CFG;
    frame.length       = 3;
    frame.payload[0]   = 1;
    frame.payload[1]   = 2;
    frame.payload[2]   = 3;

    LONGS_EQUAL(STATUS_OK, dispatch_command(&frame, &resp));

    LONGS_EQUAL(GPIO_CONTROLLER_CALL_IO_CFG, GpioControllerSpy_GetLastCall());
    LONGS_EQUAL(3, GpioControllerSpy_GetLastLength());
    MEMCMP_EQUAL(frame.payload, GpioControllerSpy_GetLastPayload(), 3);
    CHECK_TRUE(resp.ack);
}

// GPIO_WRITE (0x31) should route to gpio_controller_write().
TEST(CommandDispatcher, DispatchRoutesGpioWriteOpcode)
{
    frame.opcode = OPCODE_GPIO_WRITE;
    frame.length = 3;

    LONGS_EQUAL(STATUS_OK, dispatch_command(&frame, &resp));
    LONGS_EQUAL(GPIO_CONTROLLER_CALL_WRITE, GpioControllerSpy_GetLastCall());
    CHECK_TRUE(resp.ack);
}

// GPIO_READ (0x32) should route to gpio_controller_read() and copy the
// reported pin state into the response.
TEST(CommandDispatcher, DispatchRoutesGpioReadOpcodeAndSetsRespState)
{
    GpioControllerSpy_SetReadState(true);

    frame.opcode = OPCODE_GPIO_READ;
    frame.length = 2;

    LONGS_EQUAL(STATUS_OK, dispatch_command(&frame, &resp));
    LONGS_EQUAL(GPIO_CONTROLLER_CALL_READ, GpioControllerSpy_GetLastCall());
    CHECK_TRUE(resp.ack);
    LONGS_EQUAL(true, resp.state);
    CHECK_TRUE(resp.has_state);
}

// Opcodes other than GPIO_READ carry no meaningful state, so has_state must
// stay false even on a successful ack.
TEST(CommandDispatcher, DispatchLeavesHasStateFalseForNonReadOpcodes)
{
    frame.opcode = OPCODE_GPIO_CFG;
    frame.length = 3;

    LONGS_EQUAL(STATUS_OK, dispatch_command(&frame, &resp));
    CHECK_TRUE(resp.ack);
    CHECK_FALSE(resp.has_state);
}

// GPIO_IRQ_BIND (0x33) should route to gpio_controller_irq_bind().
TEST(CommandDispatcher, DispatchRoutesGpioIrqBindOpcode)
{
    frame.opcode = OPCODE_GPIO_IRQ_BIND;
    frame.length = 6;

    LONGS_EQUAL(STATUS_OK, dispatch_command(&frame, &resp));
    LONGS_EQUAL(GPIO_CONTROLLER_CALL_IRQ_BIND, GpioControllerSpy_GetLastCall());
    CHECK_TRUE(resp.ack);
}

// GPIO_IRQ_CFG (0x34) should route to gpio_controller_irq_cfg().
TEST(CommandDispatcher, DispatchRoutesGpioIrqCfgOpcode)
{
    frame.opcode = OPCODE_GPIO_IRQ_CFG;
    frame.length = 3;

    LONGS_EQUAL(STATUS_OK, dispatch_command(&frame, &resp));
    LONGS_EQUAL(GPIO_CONTROLLER_CALL_IRQ_CFG, GpioControllerSpy_GetLastCall());
    CHECK_TRUE(resp.ack);
}

// GPIO_IRQ_UNBIND (0x35) should route to gpio_controller_irq_unbind().
TEST(CommandDispatcher, DispatchRoutesGpioIrqUnbindOpcode)
{
    frame.opcode = OPCODE_GPIO_IRQ_UNBIND;
    frame.length = 2;

    LONGS_EQUAL(STATUS_OK, dispatch_command(&frame, &resp));
    LONGS_EQUAL(GPIO_CONTROLLER_CALL_IRQ_UNBIND, GpioControllerSpy_GetLastCall());
    CHECK_TRUE(resp.ack);
}

// An opcode that doesn't match any known command should be rejected without
// calling into gpio_controller at all.
TEST(CommandDispatcher, DispatchRejectsUnknownOpcode)
{
    frame.opcode = 0xFF;

    LONGS_EQUAL(STATUS_ERR_UNSUPPORTED, dispatch_command(&frame, &resp));
    LONGS_EQUAL(GPIO_CONTROLLER_CALL_NONE, GpioControllerSpy_GetLastCall());
    CHECK_FALSE(resp.ack);
}

// A failure from gpio_controller must be propagated, and the response must
// not be acked.
TEST(CommandDispatcher, DispatchPropagatesGpioControllerFailureAndLeavesAckFalse)
{
    GpioControllerSpy_SetReturnStatus(STATUS_ERR_INVALID_PIN);

    frame.opcode = OPCODE_GPIO_CFG;
    frame.length = 3;

    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, dispatch_command(&frame, &resp));
    CHECK_FALSE(resp.ack);
}
