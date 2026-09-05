#include <cstring>
#include "CppUTest/TestHarness.h"

extern "C"
{
#include "command_dispatcher.h"
#include "gpio_controller_spy.h"
#include "pwm_controller_spy.h"
}

#define OPCODE_GPIO_CFG        0x30U
#define OPCODE_GPIO_WRITE      0x31U
#define OPCODE_GPIO_READ       0x32U
#define OPCODE_GPIO_IRQ_BIND   0x33U
#define OPCODE_GPIO_IRQ_CFG    0x34U
#define OPCODE_GPIO_IRQ_UNBIND 0x35U

#define OPCODE_PWM_GROUP_CFG     0x40U
#define OPCODE_PWM_CFG           0x41U
#define OPCODE_PWM_SET           0x42U
#define OPCODE_PWM_RELEASE       0x43U
#define OPCODE_PWM_GET           0x44U
#define OPCODE_PWM_GROUP_GET     0x45U
#define OPCODE_PWM_GROUP_RELEASE 0x46U

TEST_GROUP(CommandDispatcher)
{
    command_frame_t frame;
    response_frame_t resp;

    void setup() override
    {
        GpioControllerSpy_Reset();
        PwmControllerSpy_Reset();
        frame = command_frame_t();
        resp  = response_frame_t();
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
    frame.opcode     = OPCODE_GPIO_CFG;
    frame.length     = 3;
    frame.payload[0] = 1;
    frame.payload[1] = 2;
    frame.payload[2] = 3;

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
    LONGS_EQUAL(1, resp.data_len);
    LONGS_EQUAL(1, resp.data[0]);
}

// Opcodes other than GPIO_READ carry no response data, so data_len must stay
// zero even on a successful ack.
TEST(CommandDispatcher, DispatchLeavesDataLenZeroForNonReadOpcodes)
{
    frame.opcode = OPCODE_GPIO_CFG;
    frame.length = 3;

    LONGS_EQUAL(STATUS_OK, dispatch_command(&frame, &resp));
    CHECK_TRUE(resp.ack);
    LONGS_EQUAL(0, resp.data_len);
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
// calling into gpio_controller at all, and the NACK carries the reason.
TEST(CommandDispatcher, DispatchRejectsUnknownOpcode)
{
    frame.opcode = 0xFF;

    LONGS_EQUAL(STATUS_ERR_UNSUPPORTED, dispatch_command(&frame, &resp));
    LONGS_EQUAL(GPIO_CONTROLLER_CALL_NONE, GpioControllerSpy_GetLastCall());
    CHECK_FALSE(resp.ack);
    LONGS_EQUAL(1, resp.data_len);
    LONGS_EQUAL(STATUS_ERR_UNSUPPORTED, resp.data[0]);
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

// A NACK carries the reason it failed as its single data byte, so the host can
// tell "already in use" from "bad pin" rather than just seeing a refusal.
TEST(CommandDispatcher, DispatchPutsFailureReasonInResponseData)
{
    GpioControllerSpy_SetReturnStatus(STATUS_ERR_BUSY);

    frame.opcode = OPCODE_GPIO_CFG;
    frame.length = 3;

    LONGS_EQUAL(STATUS_ERR_BUSY, dispatch_command(&frame, &resp));
    LONGS_EQUAL(1, resp.data_len);
    LONGS_EQUAL(STATUS_ERR_BUSY, resp.data[0]);
}

// A read that fails replies with the reason, not with its usual read value -
// the one data byte is the status, not a pin state the driver never produced.
TEST(CommandDispatcher, DispatchReadFailureReplacesReadDataWithReason)
{
    GpioControllerSpy_SetReturnStatus(STATUS_ERR_INVALID_STATE);

    frame.opcode = OPCODE_GPIO_READ;
    frame.length = 2;

    LONGS_EQUAL(STATUS_ERR_INVALID_STATE, dispatch_command(&frame, &resp));
    CHECK_FALSE(resp.ack);
    LONGS_EQUAL(1, resp.data_len);
    LONGS_EQUAL(STATUS_ERR_INVALID_STATE, resp.data[0]);
}

/* --- dispatch_command, PWM opcodes --- */

// PWM_GROUP_CFG (0x40) should route to pwm_controller_group_cfg(), forwarding
// the frame's payload/length unchanged, and ack the response on success.
TEST(CommandDispatcher, DispatchRoutesPwmGroupCfgOpcodeAndForwardsPayload)
{
    frame.opcode     = OPCODE_PWM_GROUP_CFG;
    frame.length     = 5;
    frame.payload[0] = 0xA0;
    frame.payload[1] = 0x86;
    frame.payload[2] = 0x01;
    frame.payload[3] = 0x00;
    frame.payload[4] = 1;

    LONGS_EQUAL(STATUS_OK, dispatch_command(&frame, &resp));

    LONGS_EQUAL(PWM_CONTROLLER_CALL_GROUP_CFG, PwmControllerSpy_GetLastCall());
    LONGS_EQUAL(5, PwmControllerSpy_GetLastLength());
    MEMCMP_EQUAL(frame.payload, PwmControllerSpy_GetLastPayload(), 5);
    CHECK_TRUE(resp.ack);
}

// PWM_CFG (0x41) should route to pwm_controller_channel_cfg().
TEST(CommandDispatcher, DispatchRoutesPwmChannelCfgOpcode)
{
    frame.opcode = OPCODE_PWM_CFG;
    frame.length = 3;

    LONGS_EQUAL(STATUS_OK, dispatch_command(&frame, &resp));
    LONGS_EQUAL(PWM_CONTROLLER_CALL_CHANNEL_CFG, PwmControllerSpy_GetLastCall());
    CHECK_TRUE(resp.ack);
}

// PWM_SET (0x42) should route to pwm_controller_channel_set().
TEST(CommandDispatcher, DispatchRoutesPwmChannelSetOpcode)
{
    frame.opcode = OPCODE_PWM_SET;
    frame.length = 4;

    LONGS_EQUAL(STATUS_OK, dispatch_command(&frame, &resp));
    LONGS_EQUAL(PWM_CONTROLLER_CALL_CHANNEL_SET, PwmControllerSpy_GetLastCall());
    CHECK_TRUE(resp.ack);
}

// PWM_RELEASE (0x43) should route to pwm_controller_channel_release().
TEST(CommandDispatcher, DispatchRoutesPwmChannelReleaseOpcode)
{
    frame.opcode = OPCODE_PWM_RELEASE;
    frame.length = 2;

    LONGS_EQUAL(STATUS_OK, dispatch_command(&frame, &resp));
    LONGS_EQUAL(PWM_CONTROLLER_CALL_CHANNEL_RELEASE, PwmControllerSpy_GetLastCall());
    CHECK_TRUE(resp.ack);
}

// PWM_GROUP_RELEASE (0x46) should route to pwm_controller_group_release().
TEST(CommandDispatcher, DispatchRoutesPwmGroupReleaseOpcode)
{
    frame.opcode = OPCODE_PWM_GROUP_RELEASE;
    frame.length = 1;

    LONGS_EQUAL(STATUS_OK, dispatch_command(&frame, &resp));
    LONGS_EQUAL(PWM_CONTROLLER_CALL_GROUP_RELEASE, PwmControllerSpy_GetLastCall());
    CHECK_TRUE(resp.ack);
}

// PWM_GROUP_GET (0x45) should route to pwm_controller_group_get() and serialise
// the achieved frequency as 4 little-endian bytes. 100000 Hz has four distinct
// bytes, so a byte-swapped or truncated write can't pass.
TEST(CommandDispatcher, DispatchRoutesPwmGroupGetOpcodeAndSerialisesFrequencyLittleEndian)
{
    PwmControllerSpy_SetFrequency(100000);

    frame.opcode = OPCODE_PWM_GROUP_GET;
    frame.length = 1;

    LONGS_EQUAL(STATUS_OK, dispatch_command(&frame, &resp));
    LONGS_EQUAL(PWM_CONTROLLER_CALL_GROUP_GET, PwmControllerSpy_GetLastCall());
    CHECK_TRUE(resp.ack);
    LONGS_EQUAL(4, resp.data_len);
    LONGS_EQUAL(0xA0, resp.data[0]);
    LONGS_EQUAL(0x86, resp.data[1]);
    LONGS_EQUAL(0x01, resp.data[2]);
    LONGS_EQUAL(0x00, resp.data[3]);
}

// PWM_GET (0x44) should route to pwm_controller_channel_get() and serialise the
// duty as 2 little-endian bytes. 375 (0x0177) has two distinct non-zero bytes,
// so a byte-swapped write can't pass.
TEST(CommandDispatcher, DispatchRoutesPwmChannelGetOpcodeAndSerialisesDutyLittleEndian)
{
    PwmControllerSpy_SetDuty(375);

    frame.opcode = OPCODE_PWM_GET;
    frame.length = 2;

    LONGS_EQUAL(STATUS_OK, dispatch_command(&frame, &resp));
    LONGS_EQUAL(PWM_CONTROLLER_CALL_CHANNEL_GET, PwmControllerSpy_GetLastCall());
    CHECK_TRUE(resp.ack);
    LONGS_EQUAL(2, resp.data_len);
    LONGS_EQUAL(0x77, resp.data[0]);
    LONGS_EQUAL(0x01, resp.data[1]);
}

// A pin reading low must serialise as 0x00. The high case above passes for a
// handler that hardcodes 1, so this is what pins the conversion down.
TEST(CommandDispatcher, DispatchSerialisesGpioReadLowAsZero)
{
    GpioControllerSpy_SetReadState(false);

    frame.opcode = OPCODE_GPIO_READ;
    frame.length = 2;

    LONGS_EQUAL(STATUS_OK, dispatch_command(&frame, &resp));
    CHECK_TRUE(resp.ack);
    LONGS_EQUAL(1, resp.data_len);
    LONGS_EQUAL(0, resp.data[0]);
}

// A NACK is one reason byte whatever the opcode's read width is. PWM_GROUP_GET
// acks with 4 bytes, so a dispatcher that set data_len from the table before
// checking the status would tell the host to expect 4 bytes on a failure.
TEST(CommandDispatcher, DispatchFailedWideReadRepliesWithOneReasonByte)
{
    PwmControllerSpy_SetReturnStatus(STATUS_ERR_NOT_INIT);
    PwmControllerSpy_SetFrequency(100000);

    frame.opcode = OPCODE_PWM_GROUP_GET;
    frame.length = 1;

    LONGS_EQUAL(STATUS_ERR_NOT_INIT, dispatch_command(&frame, &resp));
    CHECK_FALSE(resp.ack);
    LONGS_EQUAL(1, resp.data_len);
    LONGS_EQUAL(STATUS_ERR_NOT_INIT, resp.data[0]);
}

