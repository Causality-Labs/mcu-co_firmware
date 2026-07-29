#include <cstring>
#include "CppUTest/TestHarness.h"

extern "C" {
#include "gpio_controller.h"
#include "gpio_irq_bindings.h"
#include "gpio_spy.h"
}

/* gpio_controller_irq_cfg's private edge-byte encoding (not exported via
 * gpio_controller.h): 0 = off/disarm, 1 = rising, 2 = falling, 3 = both. */
#define WIRE_EDGE_OFF     0
#define WIRE_EDGE_RISING  1
#define WIRE_EDGE_FALLING 2
#define WIRE_EDGE_BOTH    3

TEST_GROUP(GpioController)
{
    void setup() override
    {
        GpioSpy_Reset();
        memset(irq_bindings, 0, sizeof(irq_bindings));
    }
};

/* --- gpio_controller_io_cfg --- */

// A NULL payload should be rejected.
TEST(GpioController, IoCfgRejectsNullPayload)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_io_cfg(NULL, 3));
}

// The payload must be exactly 3 bytes (dir, port, pin); shorter or longer
// should both be rejected.
TEST(GpioController, IoCfgRejectsWrongLength)
{
    uint8_t payload[3] = {GPIO_MODE_OUTPUT, 0, 0};
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_io_cfg(payload, 2));
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_io_cfg(payload, 4));
}

// Only GPIO_MODE_INPUT/GPIO_MODE_OUTPUT are valid over the wire - AF/ANALOG
// are real gpio_mode_t values elsewhere but not exposed by this command.
TEST(GpioController, IoCfgRejectsInvalidDirection)
{
    uint8_t payload_af[3]     = {GPIO_MODE_AF, 0, 0};
    uint8_t payload_analog[3] = {GPIO_MODE_ANALOG, 0, 0};

    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_io_cfg(payload_af, 3));
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_io_cfg(payload_analog, 3));
}

// port must be a valid port index (0..GPIO_NUM_OF_PORTS-1).
TEST(GpioController, IoCfgRejectsPortOutOfRange)
{
    uint8_t payload[3] = {GPIO_MODE_OUTPUT, GPIO_NUM_OF_PORTS, 0};
    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, gpio_controller_io_cfg(payload, 3));
}

// pin must be within 0..MAX_PIN_COUNT.
TEST(GpioController, IoCfgRejectsPinOutOfRange)
{
    uint8_t payload[3] = {GPIO_MODE_OUTPUT, 0, MAX_PIN_COUNT + 1};
    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, gpio_controller_io_cfg(payload, 3));
}

// Happy path: valid input should translate the wire bytes into a correctly
// populated gpio_pin_t and gpio_config_t passed to gpio_init().
TEST(GpioController, IoCfgCallsGpioInitWithCorrectPinAndConfig)
{
    uint8_t payload[3] = {GPIO_MODE_OUTPUT, GPIO_PORT_B, 5};

    LONGS_EQUAL(STATUS_OK, gpio_controller_io_cfg(payload, 3));

    gpio_pin_t pin = GpioSpy_GetLastInitPin();
    LONGS_EQUAL(GPIO_PORT_B, pin.port);
    LONGS_EQUAL(5, pin.pin);

    gpio_config_t config = GpioSpy_GetLastInitConfig();
    LONGS_EQUAL(GPIO_MODE_OUTPUT, config.mode);
    LONGS_EQUAL(GPIO_TYPE_PUSH_PULL, config.type);
    LONGS_EQUAL(GPIO_SPEED_LOW, config.speed);
    LONGS_EQUAL(GPIO_PULL_NONE, config.pull);
}

// A failure from gpio_init() must be propagated, not swallowed.
TEST(GpioController, IoCfgPropagatesGpioInitFailure)
{
    GpioSpy_SetReturnStatus(STATUS_ERR_NOT_INIT);

    uint8_t payload[3] = {GPIO_MODE_OUTPUT, GPIO_PORT_A, 0};
    LONGS_EQUAL(STATUS_ERR_NOT_INIT, gpio_controller_io_cfg(payload, 3));
}

/* --- gpio_controller_write --- */

// A NULL payload should be rejected.
TEST(GpioController, WriteRejectsNullPayload)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_write(NULL, 3));
}

// The payload must be exactly 3 bytes (level, port, pin); shorter or longer
// should both be rejected.
TEST(GpioController, WriteRejectsWrongLength)
{
    uint8_t payload[3] = {GPIO_HIGH, 0, 0};
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_write(payload, 2));
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_write(payload, 4));
}

// Only GPIO_LOW/GPIO_HIGH are valid wire levels.
TEST(GpioController, WriteRejectsInvalidLevel)
{
    uint8_t payload[3] = {2, 0, 0};
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_write(payload, 3));
}

// port must be a valid port index (0..GPIO_NUM_OF_PORTS-1).
TEST(GpioController, WriteRejectsPortOutOfRange)
{
    uint8_t payload[3] = {GPIO_HIGH, GPIO_NUM_OF_PORTS, 0};
    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, gpio_controller_write(payload, 3));
}

// pin must be within 0..MAX_PIN_COUNT.
TEST(GpioController, WriteRejectsPinOutOfRange)
{
    uint8_t payload[3] = {GPIO_HIGH, 0, MAX_PIN_COUNT + 1};
    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, gpio_controller_write(payload, 3));
}

// Happy path: valid input should call gpio_set_state() with the decoded
// pin and level.
TEST(GpioController, WriteCallsGpioSetStateWithCorrectPinAndLevel)
{
    uint8_t payload[3] = {GPIO_HIGH, GPIO_PORT_C, 7};

    LONGS_EQUAL(STATUS_OK, gpio_controller_write(payload, 3));

    gpio_pin_t pin = GpioSpy_GetLastSetStatePin();
    LONGS_EQUAL(GPIO_PORT_C, pin.port);
    LONGS_EQUAL(7, pin.pin);
    LONGS_EQUAL(GPIO_HIGH, GpioSpy_GetLastSetStateState());
}

// A failure from gpio_set_state() must be propagated, not swallowed.
TEST(GpioController, WritePropagatesGpioSetStateFailure)
{
    GpioSpy_SetReturnStatus(STATUS_ERR_INVALID_STATE);

    uint8_t payload[3] = {GPIO_LOW, GPIO_PORT_A, 0};
    LONGS_EQUAL(STATUS_ERR_INVALID_STATE, gpio_controller_write(payload, 3));
}

/* --- gpio_controller_read --- */

// A NULL payload should be rejected.
TEST(GpioController, ReadRejectsNullPayload)
{
    bool state = false;
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_read(NULL, 2, &state));
}

// A NULL output state pointer should be rejected.
TEST(GpioController, ReadRejectsNullState)
{
    uint8_t payload[2] = {0, 0};
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_read(payload, 2, NULL));
}

// The payload must be exactly 2 bytes (port, pin); shorter or longer should
// both be rejected.
TEST(GpioController, ReadRejectsWrongLength)
{
    uint8_t payload[2] = {0, 0};
    bool state = false;
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_read(payload, 1, &state));
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_read(payload, 3, &state));
}

// port must be a valid port index.
TEST(GpioController, ReadRejectsPortOutOfRange)
{
    uint8_t payload[2] = {GPIO_NUM_OF_PORTS, 0};
    bool state = false;
    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, gpio_controller_read(payload, 2, &state));
}

// pin must be within 0..MAX_PIN_COUNT.
TEST(GpioController, ReadRejectsPinOutOfRange)
{
    uint8_t payload[2] = {0, MAX_PIN_COUNT + 1};
    bool state = false;
    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, gpio_controller_read(payload, 2, &state));
}

// Happy path: valid input should call gpio_read() with the decoded pin and
// pass its reported value back out to the caller.
TEST(GpioController, ReadCallsGpioReadWithCorrectPinAndReturnsState)
{
    GpioSpy_SetReadState(true);

    uint8_t payload[2] = {GPIO_PORT_D, 9};
    bool state = false;

    LONGS_EQUAL(STATUS_OK, gpio_controller_read(payload, 2, &state));
    CHECK_TRUE(state);

    gpio_pin_t pin = GpioSpy_GetLastReadPin();
    LONGS_EQUAL(GPIO_PORT_D, pin.port);
    LONGS_EQUAL(9, pin.pin);
}

// A failure from gpio_read() must be propagated, not swallowed.
TEST(GpioController, ReadPropagatesGpioReadFailure)
{
    GpioSpy_SetReturnStatus(STATUS_ERR_INVALID_STATE);

    uint8_t payload[2] = {GPIO_PORT_A, 0};
    bool state = false;
    LONGS_EQUAL(STATUS_ERR_INVALID_STATE, gpio_controller_read(payload, 2, &state));
}

/* --- gpio_controller_irq_cfg --- */

// A NULL payload should be rejected.
TEST(GpioController, IrqCfgRejectsNullPayload)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_irq_cfg(NULL, 3));
}

// The payload must be exactly 3 bytes (edge, port, pin); shorter or longer
// should both be rejected.
TEST(GpioController, IrqCfgRejectsWrongLength)
{
    uint8_t payload[3] = {WIRE_EDGE_RISING, 0, 0};
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_irq_cfg(payload, 2));
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_irq_cfg(payload, 4));
}

// port must be a valid port index.
TEST(GpioController, IrqCfgRejectsPortOutOfRange)
{
    uint8_t payload[3] = {WIRE_EDGE_RISING, GPIO_NUM_OF_PORTS, 0};
    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, gpio_controller_irq_cfg(payload, 3));
}

// pin must be within 0..MAX_PIN_COUNT.
TEST(GpioController, IrqCfgRejectsPinOutOfRange)
{
    uint8_t payload[3] = {WIRE_EDGE_RISING, 0, MAX_PIN_COUNT + 1};
    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, gpio_controller_irq_cfg(payload, 3));
}

// Any edge byte other than off/rising/falling/both should be rejected.
TEST(GpioController, IrqCfgRejectsInvalidEdge)
{
    uint8_t payload[3] = {4, 0, 0};
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_irq_cfg(payload, 3));
}

// edge = off should disarm via gpio_deinit_interrupt() with the decoded
// pin, and clear any existing binding on that pin.
TEST(GpioController, IrqCfgEdgeOffCallsGpioDeinitInterruptAndClearsBinding)
{
    irq_bindings[6].active = true; // pre-existing binding to be cleared

    uint8_t payload[3] = {WIRE_EDGE_OFF, GPIO_PORT_E, 6};
    LONGS_EQUAL(STATUS_OK, gpio_controller_irq_cfg(payload, 3));

    gpio_pin_t pin = GpioSpy_GetLastDeinitInterruptPin();
    LONGS_EQUAL(GPIO_PORT_E, pin.port);
    LONGS_EQUAL(6, pin.pin);
    CHECK_FALSE(irq_bindings[6].active);
}

// If gpio_deinit_interrupt() fails, the function reports a generic error
// rather than propagating the underlying status code.
TEST(GpioController, IrqCfgEdgeOffReturnsErrOnDeinitFailure)
{
    GpioSpy_SetReturnStatus(STATUS_ERR_INVALID_PIN);

    uint8_t payload[3] = {WIRE_EDGE_OFF, GPIO_PORT_A, 0};
    LONGS_EQUAL(STATUS_ERR, gpio_controller_irq_cfg(payload, 3));
}

// A valid edge should arm the interrupt via gpio_init_interrupt() with the
// decoded pin, the matching trigger, the pin's dispatch-table callback, and
// the module's fixed default priority.
TEST(GpioController, IrqCfgArmsInterruptWithCorrectTriggerAndCallback)
{
    uint8_t payload[3] = {WIRE_EDGE_RISING, GPIO_PORT_B, 2};
    LONGS_EQUAL(STATUS_OK, gpio_controller_irq_cfg(payload, 3));

    gpio_pin_t pin = GpioSpy_GetLastInitInterruptPin();
    LONGS_EQUAL(GPIO_PORT_B, pin.port);
    LONGS_EQUAL(2, pin.pin);

    gpio_irq_config_t cfg = GpioSpy_GetLastInitInterruptConfig();
    LONGS_EQUAL(RISING, cfg.trigger);
    CHECK_TRUE(cfg.callback == irq_dispatch_table[2]);
    LONGS_EQUAL(5, cfg.priority); // GPIO_IRQ_DEFAULT_PRIORITY, not exported
}

// If gpio_init_interrupt() fails, the function reports a generic error
// rather than propagating the underlying status code.
TEST(GpioController, IrqCfgReturnsErrOnInitInterruptFailure)
{
    GpioSpy_SetReturnStatus(STATUS_ERR_NOT_INIT);

    uint8_t payload[3] = {WIRE_EDGE_BOTH, GPIO_PORT_A, 0};
    LONGS_EQUAL(STATUS_ERR, gpio_controller_irq_cfg(payload, 3));
}

/* --- gpio_controller_irq_bind --- */

// A NULL payload should be rejected.
TEST(GpioController, IrqBindRejectsNullPayload)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_irq_bind(NULL, 6));
}

// The payload must be exactly 6 bytes (edge, in_port, in_pin, action,
// out_port, out_pin); shorter or longer should both be rejected.
TEST(GpioController, IrqBindRejectsWrongLength)
{
    uint8_t payload[6] = {WIRE_EDGE_RISING, 0, 0, IRQ_ACTION_HIGH, 0, 1};
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_irq_bind(payload, 5));
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_irq_bind(payload, 7));
}

// Any of the two port/pin pairs being out of range should be rejected.
TEST(GpioController, IrqBindRejectsPinOutOfRange)
{
    uint8_t payload[6] = {WIRE_EDGE_RISING, GPIO_NUM_OF_PORTS, 0, IRQ_ACTION_HIGH, 0, 1};
    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, gpio_controller_irq_bind(payload, 6));
}

// Unlike irq_cfg, "off" is not a valid edge here - only rising/falling/both.
TEST(GpioController, IrqBindRejectsInvalidEdge)
{
    uint8_t payload[6] = {WIRE_EDGE_OFF, 0, 0, IRQ_ACTION_HIGH, 0, 1};
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_irq_bind(payload, 6));
}

// action must be LOW, HIGH, or TOGGLE.
TEST(GpioController, IrqBindRejectsInvalidAction)
{
    uint8_t payload[6] = {WIRE_EDGE_RISING, 0, 0, 3, 0, 1};
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_irq_bind(payload, 6));
}

// The input pin must actually be configured as an input.
TEST(GpioController, IrqBindRejectsWhenInputPinNotConfiguredAsInput)
{
    GpioSpy_SetIsInput(false);
    GpioSpy_SetIsOutput(true);

    uint8_t payload[6] = {WIRE_EDGE_RISING, 0, 0, IRQ_ACTION_HIGH, 0, 1};
    LONGS_EQUAL(STATUS_ERR_INVALID_STATE, gpio_controller_irq_bind(payload, 6));
}

// The output pin must actually be configured as an output.
TEST(GpioController, IrqBindRejectsWhenOutputPinNotConfiguredAsOutput)
{
    GpioSpy_SetIsInput(true);
    GpioSpy_SetIsOutput(false);

    uint8_t payload[6] = {WIRE_EDGE_RISING, 0, 0, IRQ_ACTION_HIGH, 0, 1};
    LONGS_EQUAL(STATUS_ERR_INVALID_STATE, gpio_controller_irq_bind(payload, 6));
}

// Binding a pin that's already bound should be rejected rather than
// silently replacing the existing binding.
TEST(GpioController, IrqBindRejectsWhenPinAlreadyBound)
{
    irq_bindings[0].active = true;

    uint8_t payload[6] = {WIRE_EDGE_RISING, 0, 0, IRQ_ACTION_HIGH, 0, 1};
    LONGS_EQUAL(STATUS_ERR_BUSY, gpio_controller_irq_bind(payload, 6));
}

// Happy path: a valid bind should store the input pin, output pin, and
// action, and mark the binding active - keyed by the input pin number.
TEST(GpioController, IrqBindStoresBindingOnSuccess)
{
    GpioSpy_SetIsInput(true);
    GpioSpy_SetIsOutput(true);

    uint8_t payload[6] = {WIRE_EDGE_FALLING, GPIO_PORT_A, 3, IRQ_ACTION_TOGGLE, GPIO_PORT_B, 4};
    LONGS_EQUAL(STATUS_OK, gpio_controller_irq_bind(payload, 6));

    CHECK_TRUE(irq_bindings[3].active);
    LONGS_EQUAL(GPIO_PORT_A, irq_bindings[3].input_pin.port);
    LONGS_EQUAL(3, irq_bindings[3].input_pin.pin);
    LONGS_EQUAL(GPIO_PORT_B, irq_bindings[3].output_pin.port);
    LONGS_EQUAL(4, irq_bindings[3].output_pin.pin);
    LONGS_EQUAL(IRQ_ACTION_TOGGLE, irq_bindings[3].action);
}

/* --- gpio_controller_irq_unbind --- */

// A NULL payload should be rejected.
TEST(GpioController, IrqUnbindRejectsNullPayload)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_irq_unbind(NULL, 2));
}

// The payload must be exactly 2 bytes (port, pin); shorter or longer should
// both be rejected.
TEST(GpioController, IrqUnbindRejectsWrongLength)
{
    uint8_t payload[2] = {0, 0};
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_irq_unbind(payload, 1));
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, gpio_controller_irq_unbind(payload, 3));
}

// port must be a valid port index.
TEST(GpioController, IrqUnbindRejectsPortOutOfRange)
{
    uint8_t payload[2] = {GPIO_NUM_OF_PORTS, 0};
    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, gpio_controller_irq_unbind(payload, 2));
}

// pin must be within 0..MAX_PIN_COUNT.
TEST(GpioController, IrqUnbindRejectsPinOutOfRange)
{
    uint8_t payload[2] = {0, MAX_PIN_COUNT + 1};
    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, gpio_controller_irq_unbind(payload, 2));
}

// Unbinding a pin with no active binding should be rejected.
TEST(GpioController, IrqUnbindRejectsWhenNoActiveBinding)
{
    uint8_t payload[2] = {GPIO_PORT_A, 5};
    LONGS_EQUAL(STATUS_ERR_INVALID_STATE, gpio_controller_irq_unbind(payload, 2));
}

// The binding's recorded input port must match the requested port, even if
// the pin number matches - protects against unbinding the wrong port's pin.
TEST(GpioController, IrqUnbindRejectsWhenPortMismatch)
{
    irq_bindings[5].active         = true;
    irq_bindings[5].input_pin.port = GPIO_PORT_A;

    uint8_t payload[2] = {GPIO_PORT_B, 5}; // same pin number, different port
    LONGS_EQUAL(STATUS_ERR_INVALID_STATE, gpio_controller_irq_unbind(payload, 2));
}

// Happy path: unbinding a matching active binding should clear it.
TEST(GpioController, IrqUnbindClearsBindingOnSuccess)
{
    irq_bindings[5].active         = true;
    irq_bindings[5].input_pin.port = GPIO_PORT_A;
    irq_bindings[5].input_pin.pin  = 5;

    uint8_t payload[2] = {GPIO_PORT_A, 5};
    LONGS_EQUAL(STATUS_OK, gpio_controller_irq_unbind(payload, 2));

    CHECK_FALSE(irq_bindings[5].active);
}
