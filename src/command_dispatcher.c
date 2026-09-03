#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "gpio_controller.h"
#include "command_dispatcher.h"
#include "logger.h"

#define MODULE_NAME "COMMAND DISPATCHER"

#define GPIO_CFG        0x30U
#define GPIO_WRITE      0x31U
#define GPIO_READ       0x32U
#define GPIO_IRQ_BIND   0x33U
#define GPIO_IRQ_CFG    0x34U
#define GPIO_IRQ_UNBIND 0x35U

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

    switch (frame->opcode)
    {
    case GPIO_CFG:
    {
        status_t ret = gpio_controller_io_cfg(frame->payload, frame->length);
        if (ret != STATUS_OK)
        {
            LOG_ERROR(MODULE_NAME, "gpio_controller_io_cfg() failed: %s", status_to_str(ret));
            return reply_nack(resp, ret);
        }

        resp->ack = true;

        return STATUS_OK;
    }

    case GPIO_WRITE:
    {
        status_t ret = gpio_controller_write(frame->payload, frame->length);
        if (ret != STATUS_OK)
        {
            LOG_ERROR(MODULE_NAME, "gpio_controller_write() failed: %s", status_to_str(ret));
            return reply_nack(resp, ret);
        }

        resp->ack = true;

        return STATUS_OK;
    }

    case GPIO_READ:
    {
        bool pin_state = false;

        status_t ret = gpio_controller_read(frame->payload, frame->length, &pin_state);
        if (ret != STATUS_OK)
        {
            LOG_ERROR(MODULE_NAME, "gpio_controller_read() failed: %s", status_to_str(ret));
            return reply_nack(resp, ret);
        }

        resp->ack      = true;
        resp->data[0]  = pin_state ? 1U : 0U;
        resp->data_len = 1U;

        if (pin_state == true)
        {
            LOG_INFO(MODULE_NAME, "gpio_controller_read() pin is high.");
        }
        else
        {
            LOG_INFO(MODULE_NAME, "gpio_controller_read() pin is low.");
        }

        return STATUS_OK;
    }

    case GPIO_IRQ_BIND:
    {
        status_t ret = gpio_controller_irq_bind(frame->payload, frame->length);
        if (ret != STATUS_OK)
        {
            LOG_ERROR(MODULE_NAME, "gpio_controller_irq_bind() failed: %s", status_to_str(ret));
            return reply_nack(resp, ret);
        }

        resp->ack = true;

        return STATUS_OK;
    }

    case GPIO_IRQ_CFG:
    {
        status_t ret = gpio_controller_irq_cfg(frame->payload, frame->length);
        if (ret != STATUS_OK)
        {
            LOG_ERROR(MODULE_NAME, "gpio_controller_irq_cfg() failed: %s", status_to_str(ret));
            return reply_nack(resp, ret);
        }

        resp->ack = true;

        return STATUS_OK;
    }

    case GPIO_IRQ_UNBIND:
    {
        status_t ret = gpio_controller_irq_unbind(frame->payload, frame->length);
        if (ret != STATUS_OK)
        {
            LOG_ERROR(MODULE_NAME, "gpio_controller_irq_unbind() failed: %s", status_to_str(ret));
            return reply_nack(resp, ret);
        }

        resp->ack = true;

        return STATUS_OK;
    }

    default:
        LOG_ERROR(MODULE_NAME, "unknown opcode 0x%02x", frame->opcode);
        return reply_nack(resp, STATUS_ERR_UNSUPPORTED);
    }
}