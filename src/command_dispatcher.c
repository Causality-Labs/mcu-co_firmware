#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "gpio_controller.h"
#include "pwm_controller.h"
#include "command_dispatcher.h"
#include "logger.h"

#define MODULE_NAME "COMMAND DISPATCHER"

#define GPIO_CFG        0x30U
#define GPIO_WRITE      0x31U
#define GPIO_READ       0x32U
#define GPIO_IRQ_BIND   0x33U
#define GPIO_IRQ_CFG    0x34U
#define GPIO_IRQ_UNBIND 0x35U

#define PWM_GROUP_CFG       0x40U
#define PWM_CHANNEL_CFG     0x41U
#define PWM_CHANNEL_SET     0x42U
#define PWM_CHANNEL_RELEASE 0x43U
#define PWM_CHANNEL_GET     0x44U
#define PWM_GROUP_GET       0x45U
#define PWM_GROUP_RELEASE   0x46U

typedef status_t (*command_action_fn)(const uint8_t *payload, uint8_t length);
typedef status_t (*command_read_fn)(const uint8_t *payload, uint8_t length, uint32_t *value);

/**
 * @brief One opcode's routing: which controller call it makes, and how wide a
 *        reply that call produces.
 *
 * A command either acts (@p action) or reads (@p read), never both, so exactly
 * one of the two is set. Read handlers all report through a @c uint32_t so a
 * single call site serves every read width; @p data_len is the width the
 * protocol fixes for that opcode's ACK, and 0 for an action. @p name is the
 * subject of the failure log, which keeps one log site instead of one per
 * opcode.
 *
 * Pointers lead and the two bytes trail so the struct packs without padding;
 * rows use designated initializers to keep the opcode reading first anyway.
 */
typedef struct
{
    command_action_fn action;
    command_read_fn read;
    const char *name;
    uint8_t opcode;
    uint8_t data_len;
} command_entry_t;

/* gpio_controller_read() reports a bool, which the table's single read
 * signature widens to uint32_t so all read opcodes share one call site. */
static status_t read_gpio_pin(const uint8_t *payload, uint8_t length, uint32_t *value)
{
    bool pin_state = false;

    status_t ret = gpio_controller_read(payload, length, &pin_state);
    if (ret == STATUS_OK)
    {
        *value = pin_state ? 1U : 0U;

        LOG_INFO(MODULE_NAME, "gpio_controller_read() pin is %s", pin_state ? "high" : "low");
    }

    return ret;
}

/* pwm_controller_channel_get() reports a uint16_t duty, which the table's
 * single read signature widens to uint32_t so all read opcodes share one call
 * site. store_le() cuts it back to the two bytes the protocol puts on the wire. */
static status_t read_pwm_duty(const uint8_t *payload, uint8_t length, uint32_t *value)
{
    uint16_t duty_permille = 0U;

    status_t ret = pwm_controller_channel_get(payload, length, &duty_permille);
    if (ret == STATUS_OK)
    {
        *value = duty_permille;
    }

    return ret;
}

static const command_entry_t COMMAND_TABLE[] = {
    {.opcode = GPIO_CFG, .action = gpio_controller_io_cfg, .data_len = 0U, .name = "gpio cfg"},
    {.opcode = GPIO_WRITE, .action = gpio_controller_write, .data_len = 0U, .name = "gpio write"},
    {.opcode = GPIO_READ, .read = read_gpio_pin, .data_len = 1U, .name = "gpio read"},
    {.opcode = GPIO_IRQ_BIND, .action = gpio_controller_irq_bind, .data_len = 0U, .name = "gpio irq bind"},
    {.opcode = GPIO_IRQ_CFG, .action = gpio_controller_irq_cfg, .data_len = 0U, .name = "gpio irq cfg"},
    {.opcode = GPIO_IRQ_UNBIND, .action = gpio_controller_irq_unbind, .data_len = 0U, .name = "gpio irq unbind"},
    {.opcode = PWM_GROUP_CFG, .action = pwm_controller_group_cfg, .data_len = 0U, .name = "pwm group cfg"},
    {.opcode = PWM_CHANNEL_CFG, .action = pwm_controller_channel_cfg, .data_len = 0U, .name = "pwm channel cfg"},
    {.opcode = PWM_CHANNEL_SET, .action = pwm_controller_channel_set, .data_len = 0U, .name = "pwm channel set"},
    {.opcode = PWM_CHANNEL_RELEASE, .action = pwm_controller_channel_release, .data_len = 0U, .name = "pwm channel release"},
    {.opcode = PWM_CHANNEL_GET, .read = read_pwm_duty, .data_len = 2U, .name = "pwm channel get"},
    {.opcode = PWM_GROUP_GET, .read = pwm_controller_group_get, .data_len = 4U, .name = "pwm group get"},
    {.opcode = PWM_GROUP_RELEASE, .action = pwm_controller_group_release, .data_len = 0U, .name = "pwm group release"},
};

#define COMMAND_TABLE_LEN (sizeof(COMMAND_TABLE) / sizeof(COMMAND_TABLE[0]))

static const command_entry_t *find_command(uint8_t opcode)
{
    for (size_t i = 0U; i < COMMAND_TABLE_LEN; i++)
    {
        if (COMMAND_TABLE[i].opcode == opcode)
        {
            return &COMMAND_TABLE[i];
        }
    }

    return NULL;
}

static void store_le(uint8_t *data, uint32_t value, uint8_t width)
{
    for (uint8_t i = 0U; (i < width) && (i < TX_DATA_MAX); i++)
    {
        data[i] = (uint8_t)((value >> (8U * i)) & 0xFFU);
    }
}

/* A NACK's single data byte is the reason it failed, so the host can tell
 * "already in use" from "bad pin" instead of just seeing a refusal. Successful
 * replies use data for the value the command read, so the two never collide. */
static status_t reply_nack(response_frame_t *resp, status_t reason)
{
    resp->ack      = false;
    resp->data[0]  = (uint8_t)reason;
    resp->data_len = 1U;

    return reason;
}

status_t dispatch_command(command_frame_t *frame, response_frame_t *resp)
{
    if (frame == NULL)
    {
        LOG_ERROR(MODULE_NAME, "Frame data structure is null");
        return STATUS_ERR_INVALID_ARG;
    }

    if (resp == NULL)
    {
        LOG_ERROR(MODULE_NAME, "Response data structure is null");
        return STATUS_ERR_INVALID_ARG;
    }

    resp->ack      = false;
    resp->data_len = 0U;

    const command_entry_t *entry = find_command(frame->opcode);
    if (entry == NULL)
    {
        LOG_ERROR(MODULE_NAME, "unknown opcode 0x%02x", frame->opcode);
        return reply_nack(resp, STATUS_ERR_UNSUPPORTED);
    }

    uint32_t response = 0U;
    status_t ret;

    if (entry->read != NULL)
    {
        ret = entry->read(frame->payload, frame->length, &response);
    }
    else
    {
        ret = entry->action(frame->payload, frame->length);
    }

    if (ret != STATUS_OK)
    {
        LOG_ERROR(MODULE_NAME, "%s failed: %s", entry->name, status_to_str(ret));
        return reply_nack(resp, ret);
    }

    store_le(resp->data, response, entry->data_len);
    resp->data_len = entry->data_len;
    resp->ack      = true;

    return STATUS_OK;
}
