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

status_t dispatch_command(frame_t *frame, response_t *resp)
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

    resp->ack   = false;
    resp->state = 0U;

    switch (frame->opcode)
    {
    case GPIO_CFG:
    {
        status_t ret = gpio_controller_io_cfg(frame->payload, frame->length);
        if (ret != STATUS_OK)
        {
            LOG_ERROR(MODULE_NAME, "gpio_controller_io_cfg() returned an error.");
            return ret;
        }

        resp->ack = true;

        return STATUS_OK;
    }

    case GPIO_WRITE:
    {
        status_t ret = gpio_controller_write(frame->payload, frame->length);
        if (ret != STATUS_OK)
        {
            LOG_ERROR(MODULE_NAME, "gpio_controller_write() returned an error.");
            return ret;
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
            LOG_ERROR(MODULE_NAME, "gpio_controller_read() returned an error.");
            return ret;
        }

        resp->ack   = true;
        resp->state = pin_state;

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
            LOG_ERROR(MODULE_NAME, "gpio_controller_irq_bind() returned an error.");
            return ret;
        }

        resp->ack = true;

        return STATUS_OK;
    }

    case GPIO_IRQ_CFG:
    {
        status_t ret = gpio_controller_irq_cfg(frame->payload, frame->length);
        if (ret != STATUS_OK)
        {
            LOG_ERROR(MODULE_NAME, "gpio_controller_irq_cfg() returned an error.");
            return ret;
        }

        resp->ack = true;

        return STATUS_OK;
    }

    case GPIO_IRQ_UNBIND:
    {
        status_t ret = gpio_controller_irq_unbind(frame->payload, frame->length);
        if (ret != STATUS_OK)
        {
            LOG_ERROR(MODULE_NAME, "gpio_controller_irq_unbind() returned an error.");
            return ret;
        }

        resp->ack = true;

        return STATUS_OK;
    }

    default:
        LOG_ERROR(MODULE_NAME, "Unknown opcode.");
        return STATUS_ERR_UNSUPPORTED;
    }
}