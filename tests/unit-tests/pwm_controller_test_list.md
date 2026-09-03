# pwm_controller Test List

Plain-language scenarios to cover for `src/pwm_controller.c`, written before any
test code — per *Test-Driven Development for Embedded C* (Ch. 3.3, "Write a
Test List"). Not a spec, just a working checklist; cross items off (or add new
ones) as tests get written.

Interface (mirrors `gpio_controller.h` — raw payload in, `status_t` out, read
commands take an output parameter):

```c
status_t pwm_controller_group_cfg(const uint8_t *payload, uint8_t length);   /* 0x40 [FREQ_LE32, GROUP]  len 5 */
status_t pwm_controller_channel_cfg(const uint8_t *payload, uint8_t length);         /* 0x41 [POL, PORT, PIN]    len 3 */
status_t pwm_controller_channel_set(const uint8_t *payload, uint8_t length);         /* 0x42 [DUTY_LE16, PORT, PIN] len 4 */
status_t pwm_controller_channel_release(const uint8_t *payload, uint8_t length);     /* 0x43 [PORT, PIN]         len 2 */
status_t pwm_controller_channel_get(const uint8_t *payload, uint8_t length, uint16_t *duty_permille);   /* 0x44 len 2 */
status_t pwm_controller_group_get(const uint8_t *payload, uint8_t length, uint32_t *frequency_hz); /* 0x45 len 1 */
status_t pwm_controller_group_release(const uint8_t *payload, uint8_t length); /* 0x46 [GROUP]     len 1 */
```

Wire encodings are in `mcu-co_Protocol.md` §7–12. All tests run against a
`fakes/timer_spy.c` standing in for `peripherals/timer.c`, which touches CMSIS
registers and can't link on the host.

## Shared across every function

- [x] Rejects a NULL `payload`
- [x] Rejects a length other than the one its opcode defines (check both one
      short and one long — an `!=` check and a `<` check both pass a single case)
- [x] Rejects an out-of-range field **before** calling the driver at all — the
      spy must record zero calls, not just a failing one

## pwm_controller_group_cfg — `[FREQ_LE32, GROUP]`

The controller is stateless: it calls `timer_init()` first and passes the
driver's status straight back, so `STATUS_ERR_BUSY` reaches the host as the
NACK reason "already in use" (decision 1 below).

- [x] Rejects a NULL payload
- [x] Rejects a length other than 5
- [x] Rejects GROUP above 2 before any driver call
- [x] Decodes FREQ little-endian (use a value with four distinct non-zero bytes,
      e.g. `A0 86 01 00` = 100000, so a byte-swapped decode can't pass)
- [x] Cold group: brings the timer up **and** starts it, in that order
- [x] Already-configured group: `timer_init()`'s `STATUS_ERR_BUSY` is returned
      verbatim, and the spy sees exactly one call — nothing is retuned or
      restarted on the way out (decision 1)
- [x] A failed start undoes the init before reporting, and the status returned
      is the start's own, not the rollback's (decision 3)
- Dropped: a separate "propagates a time-base failure" test. Now that the
  driver's status is returned verbatim, `STATUS_ERR_BUSY` above already covers
  the path; a second failure code would exercise identical code.

## pwm_controller_group_release — `[GROUP]`

- [x] Rejects a NULL payload
- [x] Rejects a length other than 1
- [x] Rejects GROUP above 2 before any driver call
- [x] Tears the group down with a single `timer_deinit()` — not a stop followed
      by a deinit, since the driver owns that sequence
- [x] Releasing a group that was never configured returns the driver's
      `STATUS_ERR_NOT_INIT` rather than `STATUS_OK` (decision 2)

## pwm_controller_channel_cfg — `[POL, PORT, PIN]`

- [x] Rejects a NULL payload
- [x] Rejects a length other than 3
- [x] Rejects POL other than 0 or 1 before any driver call
- [x] Rejects PORT/PIN outside the GPIO range before any driver call, with
      `STATUS_ERR_INVALID_PIN` to match `gpio_controller`
- [x] Resolves the pin to its timer and channel rather than encoding the map
      itself
- [x] Propagates the "no PWM channel on this pin" failure (e.g. PA0), and does
      not attempt the claim afterwards
- [x] Claims the channel at **duty 0** — the pin comes up silent, per the
      protocol decision that `cfg` carries no duty
- [x] Forwards the decoded polarity (test active-low too, or a hardcoded
      active-high passes)
- [x] Propagates the "already configured / pin owned by another driver" failure

Note: the last three went green on arrival — the single-line `return
<driver status>` written for earlier tests already satisfied them. Kept as
lock-in: they are what fails if those returns are ever wrapped in a
`return STATUS_ERR`, which would drop the reason byte from the wire.

## pwm_controller_channel_set — `[DUTY_LE16, PORT, PIN]`

- [x] Rejects a NULL payload
- [x] Rejects a length other than 4
- [x] Rejects PORT/PIN outside the GPIO range before any driver call
- [x] Resolves the pin to its timer and channel
- [x] Decodes DUTY little-endian (`E8 03` = 1000, not 59395) and forwards it to
      the driver unchanged, on the resolved channel
- [x] Propagates the driver's rejection of a duty above 1000
- [x] Propagates the "channel not configured" failure — `set` on a pin no
      `cfg` ever claimed

Note: the decode and the forward are one test, not two — the decoded value is
only observable at the driver call, so a separate decode test would assert the
same thing twice. The last two went green on arrival, kept as lock-in for the
verbatim `return timer_pwm_set_duty(...)` (same reasoning as `channel_cfg`).

## pwm_controller_channel_release — `[PORT, PIN]`

- [x] Rejects a NULL payload
- [x] Rejects a length other than 2
- [x] Rejects PORT/PIN outside the GPIO range before any driver call
- [x] Releases the channel the pin resolves to, leaving the group running
      (the spy must see a channel deinit, never a timer deinit)
- [x] Propagates the "channel not configured" failure

## pwm_controller_channel_get — `[PORT, PIN]`, out `uint16_t *`

- [x] Rejects a NULL payload
- [x] Rejects a length other than 2
- [x] Rejects a NULL output pointer
- [x] Rejects PORT/PIN outside the GPIO range before any driver call
- [x] Returns the duty the driver reports
- [x] Leaves the output parameter untouched when it fails, so a caller that
      ignores the status can't read a stale value as a real one

## pwm_controller_group_get — `[GROUP]`, out `uint32_t *`

- [x] Rejects a NULL payload
- [x] Rejects a length other than 1
- [x] Rejects a NULL output pointer
- [x] Rejects GROUP above 2 before any driver call
- [x] Returns the **achieved** frequency the driver reports, not the requested
      one (have the spy report a value deliberately off the request, e.g. 60007
      for 60000 — a controller that echoed the request would pass otherwise)
- [x] Propagates the "group not configured" failure, leaving the output
      untouched

## Decisions

1. **Reconfiguring a configured group is refused.** `group_cfg` calls
   `timer_init()` first; if the driver answers `STATUS_ERR_BUSY` the group is
   already up, and that status is returned unchanged so the host is told
   "already in use" rather than having the group silently retuned underneath
   four running pins. Returning it *verbatim* rather than flattening to
   `STATUS_ERR` is what makes the reason survive to the wire. It also means the
   controller holds **no state of its own** (decision 5): the driver already
   tracks which timers are configured, and a second copy here would drift.
2. **Releasing an unconfigured group is an error, not a no-op.** It returns the
   driver's `STATUS_ERR_NOT_INIT`. The host asked to undo something that was
   never done; a host doing unconditional startup cleanup can ignore the code,
   but one that wants to know can't recover a fact that was never sent.
3. **A failed start rolls back.** If `timer_init()` succeeds but
   `timer_start()` fails, the controller deinitialises the timer before
   returning the failure. Otherwise the group sits initialised-but-stopped, and
   a later `pwm channel cfg` on one of its pins would ACK — the timer really is
   configured — while the pin produced no output at all.
4. **One owner per validation rule.** The controller range-checks the fields it
   casts to an enum — GROUP, POL, PORT, PIN — because the cast is undefined
   behaviour otherwise, and that is what `gpio_controller` already does. Plain
   integers, FREQ and DUTY, go straight through for the driver to validate. No
   duplicated bounds that can drift apart.
5. **The controller holds no state** — follows from decision 1.
6. **`resolve_channel()` owns the PORT/PIN pair.** Four of the six commands
   carry the same two bytes and do the same two things with them: bound both
   fields, then look the pin up. Extracted once the fourth copy landed, so the
   bounds and the "never encode the pin map here" rule live in one place. The
   tests written against each command before the extraction are what hold it.

## Noted, not a decision

- `pwm channel get` on an unmapped pin (e.g. PA0) and on a pin that was simply never
  claimed both come back as the same bare NACK, so the host can't tell them
  apart. Consistent with protocol open decision #4 (no NACK reason codes) —
  recording it, not proposing a change.
