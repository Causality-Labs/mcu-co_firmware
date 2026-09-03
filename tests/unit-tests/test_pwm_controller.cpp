#include "CppUTest/TestHarness.h"

extern "C" {
#include "pwm_controller.h"
#include "timer_spy.h"
}

/* Wire payload length for PWM_GROUP_CFG (0x40): FREQ_LE32 + GROUP. */
#define GROUP_CFG_LEN 5

/* Wire payload length for PWM_GROUP_RELEASE (0x46): GROUP alone. */
#define GROUP_RELEASE_LEN 1

/* Wire payload length for PWM_CFG (0x41): POL + PORT + PIN. */
#define CFG_LEN 3

/* Wire payload length for PWM_SET (0x42): DUTY_LE16 + PORT + PIN. */
#define SET_LEN 4

/* Wire payload length for PWM_RELEASE (0x43): PORT + PIN. */
#define RELEASE_LEN 2

/* Wire payload length for PWM_GET (0x44): PORT + PIN. */
#define GET_LEN 2

/* Wire payload length for PWM_GROUP_GET (0x45): GROUP alone. */
#define GROUP_GET_LEN 1

TEST_GROUP(PwmController)
{
    void setup() override
    {
        TimerSpy_Reset();
    }
};

/* --- pwm_controller_group_cfg --- */

// A NULL payload should be rejected.
TEST(PwmController, GroupCfgRejectsNullPayload)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_group_cfg(NULL, GROUP_CFG_LEN));
}

// The payload must be exactly 5 bytes (FREQ_LE32 + GROUP); one short and one
// long are both checked, since a `< 5` check would pass the long case.
TEST(PwmController, GroupCfgRejectsWrongLength)
{
    uint8_t payload[GROUP_CFG_LEN] = {0};

    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_group_cfg(payload, GROUP_CFG_LEN - 1));
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_group_cfg(payload, GROUP_CFG_LEN + 1));
}

// GROUP picks the timer, so an out-of-range value would be cast to a
// timer_instance_t the driver never defined. It has to be caught here, and
// caught *before* the driver is touched at all - hence the zero-call check
// rather than just a failing status.
TEST(PwmController, GroupCfgRejectsGroupAboveMax)
{
    uint8_t payload[GROUP_CFG_LEN] = {0xE8, 0x03, 0x00, 0x00, TIMER_INSTANCE_COUNT};

    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_group_cfg(payload, GROUP_CFG_LEN));
    LONGS_EQUAL(0, TimerSpy_GetCallCount());
}

// A group that isn't up yet has to be configured and then started, in that
// order - starting a counter whose time base was never programmed runs the
// first period on reset values. Uses group 1, not 0, so a hardcoded
// TIMER_TIM2 can't pass.
TEST(PwmController, GroupCfgOnColdGroupInitsThenStarts)
{
    uint8_t payload[GROUP_CFG_LEN] = {0xE8, 0x03, 0x00, 0x00, 1};

    LONGS_EQUAL(STATUS_OK, pwm_controller_group_cfg(payload, GROUP_CFG_LEN));

    LONGS_EQUAL(2, TimerSpy_GetCallCount());
    LONGS_EQUAL(TIMER_CALL_INIT, TimerSpy_GetCall(0));
    LONGS_EQUAL(TIMER_CALL_START, TimerSpy_GetCall(1));
    LONGS_EQUAL(TIMER_TIM3, TimerSpy_GetLastInstance());
}

// FREQ is the first four payload bytes, little-endian. A0 86 01 00 is 100000;
// decoded the other way round it would be 0xA0860100, so a byte-swapped or
// truncated read can't pass this.
TEST(PwmController, GroupCfgDecodesFrequencyLittleEndian)
{
    uint8_t payload[GROUP_CFG_LEN] = {0xA0, 0x86, 0x01, 0x00, 0};

    LONGS_EQUAL(STATUS_OK, pwm_controller_group_cfg(payload, GROUP_CFG_LEN));

    UNSIGNED_LONGS_EQUAL(100000, TimerSpy_GetLastFrequency());
}

// A group that is already up reports STATUS_ERR_BUSY from timer_init(), and that
// is the answer the host gets: reconfiguring a live group is refused rather than
// applied, because changing a group's frequency moves every channel already
// running on it. The single recorded call is the point - nothing is retuned,
// restarted, or otherwise touched on the way out.
TEST(PwmController, GroupCfgRefusesGroupThatIsAlreadyConfigured)
{
    uint8_t payload[GROUP_CFG_LEN] = {0xA0, 0x86, 0x01, 0x00, 2};

    TimerSpy_SetReturnStatusFor(TIMER_CALL_INIT, STATUS_ERR_BUSY);

    LONGS_EQUAL(STATUS_ERR_BUSY, pwm_controller_group_cfg(payload, GROUP_CFG_LEN));

    LONGS_EQUAL(1, TimerSpy_GetCallCount());
    LONGS_EQUAL(TIMER_CALL_INIT, TimerSpy_GetCall(0));
}


// A start that fails after the time base programmed leaves the group
// configured-but-silent - and a later `pwm cfg` would happily ACK a pin on it
// that produces no output. So the init is undone before reporting, and the
// failure the host sees is the start's own status, not the deinit's.
TEST(PwmController, GroupCfgUndoesInitWhenStartFails)
{
    uint8_t payload[GROUP_CFG_LEN] = {0xA0, 0x86, 0x01, 0x00, 1};

    TimerSpy_SetReturnStatusFor(TIMER_CALL_START, STATUS_ERR_TIMEOUT);

    LONGS_EQUAL(STATUS_ERR_TIMEOUT, pwm_controller_group_cfg(payload, GROUP_CFG_LEN));

    LONGS_EQUAL(3, TimerSpy_GetCallCount());
    LONGS_EQUAL(TIMER_CALL_INIT, TimerSpy_GetCall(0));
    LONGS_EQUAL(TIMER_CALL_START, TimerSpy_GetCall(1));
    LONGS_EQUAL(TIMER_CALL_DEINIT, TimerSpy_GetCall(2));
}

/* --- pwm_controller_group_release --- */

// A NULL payload should be rejected.
TEST(PwmController, GroupReleaseRejectsNullPayload)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_group_release(NULL, GROUP_RELEASE_LEN));
}

// The payload must be exactly 1 byte (GROUP alone). Zero and two are both
// checked, since a `< 1` check would pass the long case.
TEST(PwmController, GroupReleaseRejectsWrongLength)
{
    uint8_t payload[GROUP_RELEASE_LEN + 1] = {0};

    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_group_release(payload, GROUP_RELEASE_LEN - 1));
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_group_release(payload, GROUP_RELEASE_LEN + 1));
}

// GROUP picks the timer to tear down, so an out-of-range value would be cast to
// a timer_instance_t the driver never defined. Caught here, and caught before
// the driver is touched - hence the zero-call check rather than just a failing
// status.
TEST(PwmController, GroupReleaseRejectsGroupAboveMax)
{
    uint8_t payload[GROUP_RELEASE_LEN] = {TIMER_INSTANCE_COUNT};

    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_group_release(payload, GROUP_RELEASE_LEN));
    LONGS_EQUAL(0, TimerSpy_GetCallCount());
}

// Tearing a group down is one driver call, not a stop followed by a deinit -
// timer_deinit() owns that whole sequence (counter stopped, channels dropped,
// pins released, clock gated), and splitting it here would put the ordering in
// two places. Uses group 1 so a hardcoded TIMER_TIM2 can't pass.
TEST(PwmController, GroupReleaseTearsGroupDownWithSingleDeinit)
{
    uint8_t payload[GROUP_RELEASE_LEN] = {1};

    LONGS_EQUAL(STATUS_OK, pwm_controller_group_release(payload, GROUP_RELEASE_LEN));

    LONGS_EQUAL(1, TimerSpy_GetCallCount());
    LONGS_EQUAL(TIMER_CALL_DEINIT, TimerSpy_GetCall(0));
    LONGS_EQUAL(TIMER_TIM3, TimerSpy_GetLastInstance());
}

// Releasing a group that was never configured is an error, not a no-op: the
// driver's STATUS_ERR_NOT_INIT is handed back untouched so the host is told
// what happened. This test exists to hold that reason in place - flattening
// driver failures to a generic STATUS_ERR here would compile and still ACK-vs-
// NACK correctly, while silently dropping the "why" from the wire.
TEST(PwmController, GroupReleaseOnUnconfiguredGroupReportsNotInit)
{
    uint8_t payload[GROUP_RELEASE_LEN] = {1};

    TimerSpy_SetReturnStatusFor(TIMER_CALL_DEINIT, STATUS_ERR_NOT_INIT);

    LONGS_EQUAL(STATUS_ERR_NOT_INIT, pwm_controller_group_release(payload, GROUP_RELEASE_LEN));
}

/* --- pwm_controller_channel_cfg --- */

// A NULL payload should be rejected.
TEST(PwmController, ChannelCfgRejectsNullPayload)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_channel_cfg(NULL, CFG_LEN));
}

// The payload must be exactly 3 bytes (POL + PORT + PIN); one short and one
// long are both checked, since a `< 3` check would pass the long case.
TEST(PwmController, ChannelCfgRejectsWrongLength)
{
    uint8_t payload[CFG_LEN + 1] = {0};

    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_channel_cfg(payload, CFG_LEN - 1));
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_channel_cfg(payload, CFG_LEN + 1));
}

// POL is cast to a timer_polarity_t, which defines only 0 and 1 - anything else
// would be a cast to an enumerator that doesn't exist. Rejected before the
// driver is reached, so the zero-call check, not just a failing status.
TEST(PwmController, ChannelCfgRejectsPolarityAboveMax)
{
    uint8_t payload[CFG_LEN] = {2, 0, 5};

    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_channel_cfg(payload, CFG_LEN));
    LONGS_EQUAL(0, TimerSpy_GetCallCount());
}

// PORT is cast to a gpio_port_t and PIN indexes a 16-bit register, so both are
// bounded here before the driver sees them - and both directions are checked,
// since a guard on only one would let the other through. STATUS_ERR_INVALID_PIN
// rather than INVALID_ARG, matching what gpio_controller returns for the same
// two fields.
TEST(PwmController, ChannelCfgRejectsPortOrPinOutOfRange)
{
    uint8_t bad_port[CFG_LEN] = {0, GPIO_NUM_OF_PORTS, 5};
    uint8_t bad_pin[CFG_LEN]  = {0, 0, MAX_PIN_COUNT + 1};

    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, pwm_controller_channel_cfg(bad_port, CFG_LEN));
    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, pwm_controller_channel_cfg(bad_pin, CFG_LEN));
    LONGS_EQUAL(0, TimerSpy_GetCallCount());
}

// The controller must ask the driver which timer and channel a pin belongs to
// rather than knowing the map itself - that map lives only in timer.c. The spy
// records the pin it was handed, so a controller that passed the wrong port or
// pin through is caught here even though the spy's answer is programmable.
TEST(PwmController, ChannelCfgResolvesPinThroughTheDriver)
{
    uint8_t payload[CFG_LEN] = {0, GPIO_PORT_B, 7};

    TimerSpy_SetLookupResult(TIMER_TIM4, TIMER_CH2);

    LONGS_EQUAL(STATUS_OK, pwm_controller_channel_cfg(payload, CFG_LEN));

    LONGS_EQUAL(TIMER_CALL_LOOKUP_PIN, TimerSpy_GetCall(0));
    LONGS_EQUAL(GPIO_PORT_B, TimerSpy_GetLastLookupPin().port);
    LONGS_EQUAL(7, TimerSpy_GetLastLookupPin().pin);
}

// Claiming the channel is the point of the command, and it claims at duty 0 so
// the pin comes up silent - the protocol deliberately gives `pwm channel cfg`
// no duty field, and a pin that started at some leftover compare value would
// drive an output the host never asked for. The instance and channel must be
// the ones the lookup returned, not anything the controller decided itself.
TEST(PwmController, ChannelCfgClaimsResolvedChannelAtZeroDuty)
{
    uint8_t payload[CFG_LEN] = {0, GPIO_PORT_B, 7};

    TimerSpy_SetLookupResult(TIMER_TIM4, TIMER_CH2);

    LONGS_EQUAL(STATUS_OK, pwm_controller_channel_cfg(payload, CFG_LEN));

    LONGS_EQUAL(2, TimerSpy_GetCallCount());
    LONGS_EQUAL(TIMER_CALL_LOOKUP_PIN, TimerSpy_GetCall(0));
    LONGS_EQUAL(TIMER_CALL_PWM_CHANNEL_INIT, TimerSpy_GetCall(1));
    LONGS_EQUAL(TIMER_TIM4, TimerSpy_GetLastInstance());
    LONGS_EQUAL(TIMER_CH2, TimerSpy_GetLastChannel());
    UNSIGNED_LONGS_EQUAL(0, TimerSpy_GetLastDuty());
}

// The polarity the host asked for has to reach the driver. Uses active-low
// specifically: active-high is 0, which is also what a hardcoded field or an
// uninitialised struct would produce, so only the active-low case can tell a
// real forward from an accident.
TEST(PwmController, ChannelCfgForwardsActiveLowPolarity)
{
    uint8_t payload[CFG_LEN] = {TIMER_POLARITY_ACTIVE_LOW, GPIO_PORT_A, 5};

    LONGS_EQUAL(STATUS_OK, pwm_controller_channel_cfg(payload, CFG_LEN));

    LONGS_EQUAL(TIMER_POLARITY_ACTIVE_LOW, TimerSpy_GetLastPolarity());
}

// A pin with no PWM channel (PA0, say) fails at the lookup, and that status is
// what the host gets - "this pin can't do PWM" is a different problem from
// "the pin is in use", and flattening them to a generic failure loses the
// distinction the reason byte exists to carry.
TEST(PwmController, ChannelCfgPropagatesUnmappedPinFailure)
{
    uint8_t payload[CFG_LEN] = {0, GPIO_PORT_A, 0};

    TimerSpy_SetReturnStatusFor(TIMER_CALL_LOOKUP_PIN, STATUS_ERR_UNSUPPORTED);

    LONGS_EQUAL(STATUS_ERR_UNSUPPORTED, pwm_controller_channel_cfg(payload, CFG_LEN));

    LONGS_EQUAL(1, TimerSpy_GetCallCount());
    LONGS_EQUAL(0, TimerSpy_CountCalls(TIMER_CALL_PWM_CHANNEL_INIT));
}

// A pin already claimed - by an earlier `pwm channel cfg`, or by gpio cfg -
// reports STATUS_ERR_BUSY unchanged, which is what becomes the "already in use"
// reason byte on the wire.
TEST(PwmController, ChannelCfgPropagatesAlreadyClaimedFailure)
{
    uint8_t payload[CFG_LEN] = {0, GPIO_PORT_A, 5};

    TimerSpy_SetReturnStatusFor(TIMER_CALL_PWM_CHANNEL_INIT, STATUS_ERR_BUSY);

    LONGS_EQUAL(STATUS_ERR_BUSY, pwm_controller_channel_cfg(payload, CFG_LEN));
}

/* --- pwm_controller_channel_set --- */

// A NULL payload should be rejected.
TEST(PwmController, ChannelSetRejectsNullPayload)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_channel_set(NULL, SET_LEN));
}

// The payload must be exactly 4 bytes (DUTY_LE16 + PORT + PIN); one short and
// one long are both checked, since a `< 4` check would pass the long case.
TEST(PwmController, ChannelSetRejectsWrongLength)
{
    uint8_t payload[SET_LEN + 1] = {0};

    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_channel_set(payload, SET_LEN - 1));
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_channel_set(payload, SET_LEN + 1));
}

// Same two fields as `pwm channel cfg`, bounded the same way and for the same
// reason: PORT is cast to a gpio_port_t and PIN indexes a 16-bit register. Both
// directions are checked, and the zero-call assertion is what proves the
// rejection happens before the pin is handed to the driver's lookup.
TEST(PwmController, ChannelSetRejectsPortOrPinOutOfRange)
{
    uint8_t bad_port[SET_LEN] = {0xE8, 0x03, GPIO_NUM_OF_PORTS, 5};
    uint8_t bad_pin[SET_LEN]  = {0xE8, 0x03, GPIO_PORT_A, MAX_PIN_COUNT + 1};

    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, pwm_controller_channel_set(bad_port, SET_LEN));
    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, pwm_controller_channel_set(bad_pin, SET_LEN));
    LONGS_EQUAL(0, TimerSpy_GetCallCount());
}

// `set` has to ask the driver which timer and channel the pin belongs to, the
// same as `cfg` does - the map lives only in timer.c, and a second copy here
// would be one more thing to drift. The spy records the pin it was handed, so a
// controller reading the duty bytes as the port or pin is caught here.
TEST(PwmController, ChannelSetResolvesPinThroughTheDriver)
{
    uint8_t payload[SET_LEN] = {0xE8, 0x03, GPIO_PORT_B, 7};

    TimerSpy_SetLookupResult(TIMER_TIM4, TIMER_CH2);

    LONGS_EQUAL(STATUS_OK, pwm_controller_channel_set(payload, SET_LEN));

    LONGS_EQUAL(TIMER_CALL_LOOKUP_PIN, TimerSpy_GetCall(0));
    LONGS_EQUAL(GPIO_PORT_B, TimerSpy_GetLastLookupPin().port);
    LONGS_EQUAL(7, TimerSpy_GetLastLookupPin().pin);
}

// DUTY is the first two payload bytes, little-endian: E8 03 is 1000, and read
// the other way round it would be 59395 - a value the driver would reject, so a
// byte-swapped decode can't hide behind a passing status. The duty has to land
// on the channel the lookup resolved, not on a hardcoded one, hence the
// instance and channel assertions alongside it.
TEST(PwmController, ChannelSetForwardsDecodedDutyToTheDriver)
{
    uint8_t payload[SET_LEN] = {0xE8, 0x03, GPIO_PORT_B, 7};

    TimerSpy_SetLookupResult(TIMER_TIM4, TIMER_CH2);

    LONGS_EQUAL(STATUS_OK, pwm_controller_channel_set(payload, SET_LEN));

    LONGS_EQUAL(TIMER_CALL_PWM_SET_DUTY, TimerSpy_GetCall(1));
    LONGS_EQUAL(1000, TimerSpy_GetLastDuty());
    LONGS_EQUAL(TIMER_TIM4, TimerSpy_GetLastInstance());
    LONGS_EQUAL(TIMER_CH2, TimerSpy_GetLastChannel());
}

// TIMER_DUTY_MAX is the driver's bound, not the controller's - the controller
// range-checks only the fields it casts to an enum, and duty is a plain integer
// that goes straight through. So a duty of 1001 must come back as the driver's
// own STATUS_ERR_INVALID_ARG, not a second bound checked here that could drift
// away from the driver's.
TEST(PwmController, ChannelSetPropagatesOutOfRangeDutyFailure)
{
    uint8_t payload[SET_LEN] = {0xE9, 0x03, GPIO_PORT_B, 7};

    TimerSpy_SetReturnStatusFor(TIMER_CALL_PWM_SET_DUTY, STATUS_ERR_INVALID_ARG);

    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_channel_set(payload, SET_LEN));
}

// Driving a pin that no `pwm channel cfg` ever claimed is the driver's
// STATUS_ERR_NOT_INIT, handed back untouched so the host is told which of the
// two setup steps it skipped. Flattening driver failures to a generic
// STATUS_ERR here would still NACK correctly while dropping the "why" from the
// wire - this test is what fails if that ever happens.
TEST(PwmController, ChannelSetPropagatesUnclaimedChannelFailure)
{
    uint8_t payload[SET_LEN] = {0xE8, 0x03, GPIO_PORT_B, 7};

    TimerSpy_SetReturnStatusFor(TIMER_CALL_PWM_SET_DUTY, STATUS_ERR_NOT_INIT);

    LONGS_EQUAL(STATUS_ERR_NOT_INIT, pwm_controller_channel_set(payload, SET_LEN));
}

/* --- pwm_controller_channel_release --- */

// A NULL payload should be rejected.
TEST(PwmController, ChannelReleaseRejectsNullPayload)
{
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_channel_release(NULL, RELEASE_LEN));
}

// The payload must be exactly 2 bytes (PORT + PIN); one short and one long are
// both checked, since a `< 2` check would pass the long case.
TEST(PwmController, ChannelReleaseRejectsWrongLength)
{
    uint8_t payload[RELEASE_LEN + 1] = {0};

    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_channel_release(payload, RELEASE_LEN - 1));
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_channel_release(payload, RELEASE_LEN + 1));
}

// Same two fields as the other channel commands, bounded here for the same
// reason: PORT is cast to a gpio_port_t and PIN indexes a 16-bit register. The
// zero-call assertion proves the rejection lands before the driver's lookup.
TEST(PwmController, ChannelReleaseRejectsPortOrPinOutOfRange)
{
    uint8_t bad_port[RELEASE_LEN] = {GPIO_NUM_OF_PORTS, 5};
    uint8_t bad_pin[RELEASE_LEN]  = {GPIO_PORT_A, MAX_PIN_COUNT + 1};

    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, pwm_controller_channel_release(bad_port, RELEASE_LEN));
    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, pwm_controller_channel_release(bad_pin, RELEASE_LEN));
    LONGS_EQUAL(0, TimerSpy_GetCallCount());
}

// Releasing one pin must not take the group down with it - the other three
// channels on that timer keep running, and tearing the timer down would silence
// them too. So the driver call is timer_pwm_channel_deinit() on the resolved
// channel, and timer_deinit() must never appear: that one belongs to
// `pwm group release` alone.
TEST(PwmController, ChannelReleaseDropsChannelAndLeavesTheGroupRunning)
{
    uint8_t payload[RELEASE_LEN] = {GPIO_PORT_B, 7};

    TimerSpy_SetLookupResult(TIMER_TIM4, TIMER_CH2);

    LONGS_EQUAL(STATUS_OK, pwm_controller_channel_release(payload, RELEASE_LEN));

    LONGS_EQUAL(TIMER_CALL_LOOKUP_PIN, TimerSpy_GetCall(0));
    LONGS_EQUAL(TIMER_CALL_PWM_CHANNEL_DEINIT, TimerSpy_GetCall(1));
    LONGS_EQUAL(0, TimerSpy_CountCalls(TIMER_CALL_DEINIT));
    LONGS_EQUAL(TIMER_TIM4, TimerSpy_GetLastInstance());
    LONGS_EQUAL(TIMER_CH2, TimerSpy_GetLastChannel());
}

// Releasing a pin no `pwm channel cfg` ever claimed is the driver's
// STATUS_ERR_NOT_INIT, handed back untouched rather than flattened - the host
// gets told it released nothing instead of a bare refusal.
TEST(PwmController, ChannelReleasePropagatesUnclaimedChannelFailure)
{
    uint8_t payload[RELEASE_LEN] = {GPIO_PORT_B, 7};

    TimerSpy_SetReturnStatusFor(TIMER_CALL_PWM_CHANNEL_DEINIT, STATUS_ERR_NOT_INIT);

    LONGS_EQUAL(STATUS_ERR_NOT_INIT, pwm_controller_channel_release(payload, RELEASE_LEN));
}

/* --- pwm_controller_channel_get --- */

// A NULL payload should be rejected.
TEST(PwmController, ChannelGetRejectsNullPayload)
{
    uint16_t duty = 0;

    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_channel_get(NULL, GET_LEN, &duty));
}

// The payload must be exactly 2 bytes (PORT + PIN); one short and one long are
// both checked, since a `< 2` check would pass the long case.
TEST(PwmController, ChannelGetRejectsWrongLength)
{
    uint8_t payload[GET_LEN + 1] = {0};
    uint16_t duty                = 0;

    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_channel_get(payload, GET_LEN - 1, &duty));
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_channel_get(payload, GET_LEN + 1, &duty));
}

// The duty has nowhere to go without an output pointer, so a NULL one is
// rejected before anything else happens - a read command that ACKed with no
// value written would leave the dispatcher serialising whatever was on its
// stack.
TEST(PwmController, ChannelGetRejectsNullOutputPointer)
{
    uint8_t payload[GET_LEN] = {GPIO_PORT_B, 7};

    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_channel_get(payload, GET_LEN, NULL));
    LONGS_EQUAL(0, TimerSpy_GetCallCount());
}

// Same two fields as the other channel commands, bounded here for the same
// reason, and rejected before the driver's lookup.
TEST(PwmController, ChannelGetRejectsPortOrPinOutOfRange)
{
    uint8_t bad_port[GET_LEN] = {GPIO_NUM_OF_PORTS, 5};
    uint8_t bad_pin[GET_LEN]  = {GPIO_PORT_A, MAX_PIN_COUNT + 1};
    uint16_t duty             = 0;

    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, pwm_controller_channel_get(bad_port, GET_LEN, &duty));
    LONGS_EQUAL(STATUS_ERR_INVALID_PIN, pwm_controller_channel_get(bad_pin, GET_LEN, &duty));
    LONGS_EQUAL(0, TimerSpy_GetCallCount());
}

// The duty the host gets back is whatever the driver reports for the resolved
// channel. 375 is deliberately not the value any other test sets, so a
// controller returning a hardcoded or stale duty can't pass.
TEST(PwmController, ChannelGetReturnsTheDutyTheDriverReports)
{
    uint8_t payload[GET_LEN] = {GPIO_PORT_B, 7};
    uint16_t duty            = 0;

    TimerSpy_SetLookupResult(TIMER_TIM4, TIMER_CH2);
    TimerSpy_SetDuty(375);

    LONGS_EQUAL(STATUS_OK, pwm_controller_channel_get(payload, GET_LEN, &duty));

    LONGS_EQUAL(375, duty);
    LONGS_EQUAL(TIMER_CALL_LOOKUP_PIN, TimerSpy_GetCall(0));
    LONGS_EQUAL(TIMER_CALL_PWM_GET_DUTY, TimerSpy_GetCall(1));
    LONGS_EQUAL(TIMER_TIM4, TimerSpy_GetLastInstance());
    LONGS_EQUAL(TIMER_CH2, TimerSpy_GetLastChannel());
}

// A failed read must leave the output alone. The dispatcher only serialises
// DATA on an ACK, so this is defence for a caller that ignores the status: the
// sentinel below has to survive, or a stale duty could be read back as a real
// one. Passing the driver's pointer straight through is what makes this hold -
// the driver already writes only on success.
TEST(PwmController, ChannelGetLeavesOutputUntouchedWhenTheReadFails)
{
    uint8_t payload[GET_LEN] = {GPIO_PORT_B, 7};
    uint16_t duty            = 0xBEEF;

    TimerSpy_SetDuty(375);
    TimerSpy_SetReturnStatusFor(TIMER_CALL_PWM_GET_DUTY, STATUS_ERR_NOT_INIT);

    LONGS_EQUAL(STATUS_ERR_NOT_INIT, pwm_controller_channel_get(payload, GET_LEN, &duty));

    LONGS_EQUAL(0xBEEF, duty);
}

/* --- pwm_controller_group_get --- */

// A NULL payload should be rejected.
TEST(PwmController, GroupGetRejectsNullPayload)
{
    uint32_t frequency = 0;

    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_group_get(NULL, GROUP_GET_LEN, &frequency));
}

// The payload must be exactly 1 byte (GROUP alone). Zero and two are both
// checked, since a `< 1` check would pass the long case.
TEST(PwmController, GroupGetRejectsWrongLength)
{
    uint8_t payload[GROUP_GET_LEN + 1] = {0};
    uint32_t frequency                 = 0;

    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_group_get(payload, GROUP_GET_LEN - 1, &frequency));
    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_group_get(payload, GROUP_GET_LEN + 1, &frequency));
}

// Same as the channel read: the frequency has nowhere to go without an output
// pointer, so a NULL one is rejected before the driver is touched.
TEST(PwmController, GroupGetRejectsNullOutputPointer)
{
    uint8_t payload[GROUP_GET_LEN] = {1};

    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_group_get(payload, GROUP_GET_LEN, NULL));
    LONGS_EQUAL(0, TimerSpy_GetCallCount());
}

// GROUP picks the timer to query, so an out-of-range value would be cast to a
// timer_instance_t the driver never defined - caught here, before any driver
// call.
TEST(PwmController, GroupGetRejectsGroupAboveMax)
{
    uint8_t payload[GROUP_GET_LEN] = {TIMER_INSTANCE_COUNT};
    uint32_t frequency             = 0;

    LONGS_EQUAL(STATUS_ERR_INVALID_ARG, pwm_controller_group_get(payload, GROUP_GET_LEN, &frequency));
    LONGS_EQUAL(0, TimerSpy_GetCallCount());
}

// What comes back is the frequency the hardware achieved, not the one the host
// asked for - the prescaler and reload are integers, so 60000 Hz requested can
// land at 60007. The spy reports a value deliberately off any request, so a
// controller echoing the requested frequency instead of reading the driver
// can't pass. Group 1 is used so a hardcoded TIMER_TIM2 fails too.
TEST(PwmController, GroupGetReturnsTheAchievedFrequency)
{
    uint8_t payload[GROUP_GET_LEN] = {1};
    uint32_t frequency             = 0;

    TimerSpy_SetFrequency(60007);

    LONGS_EQUAL(STATUS_OK, pwm_controller_group_get(payload, GROUP_GET_LEN, &frequency));

    UNSIGNED_LONGS_EQUAL(60007, frequency);
    LONGS_EQUAL(1, TimerSpy_GetCallCount());
    LONGS_EQUAL(TIMER_CALL_GET_FREQUENCY, TimerSpy_GetCall(0));
    LONGS_EQUAL(TIMER_TIM3, TimerSpy_GetLastInstance());
}

// Querying a group that was never configured is the driver's
// STATUS_ERR_NOT_INIT, handed back untouched, and the output is left alone -
// the sentinel has to survive so a caller that ignores the status can't read a
// stale frequency as a real one.
TEST(PwmController, GroupGetPropagatesUnconfiguredGroupFailure)
{
    uint8_t payload[GROUP_GET_LEN] = {1};
    uint32_t frequency             = 0xDEADBEEF;

    TimerSpy_SetFrequency(60007);
    TimerSpy_SetReturnStatusFor(TIMER_CALL_GET_FREQUENCY, STATUS_ERR_NOT_INIT);

    LONGS_EQUAL(STATUS_ERR_NOT_INIT, pwm_controller_group_get(payload, GROUP_GET_LEN, &frequency));

    UNSIGNED_LONGS_EQUAL(0xDEADBEEF, frequency);
}
