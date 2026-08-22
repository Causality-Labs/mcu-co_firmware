#include <stdio.h>
#include "gpio.h"
#include "uart.h"
#include "rcc.h"
#include "logger.h"
#include "systick.h"
#include "status.h"
#include "frame_parser.h"
#include "crc16.h"
#include "command_dispatcher.h"
#include "command_transport.h"

#define MODULE_NAME "MAIN"

const gpio_pin_t led                            = {.port = GPIO_PORT_A, .pin = (uint8_t)5U};
static const uart_instance_t commands_transport = UART_INSTANCE_USART2;

void button_ISR(void);

int main(void)
{
    if (rcc_init(RCC_SYSCLK_HSI_170MHZ) != STATUS_OK)
    {
        for (;;)
        {
        }
    }

    if (systick_init() != STATUS_OK)
    {
        for (;;)
        {
        }
    }

    if (logger_init() != 0)
    {
        for (;;)
        {
        }
    }

    if (command_transport_init(commands_transport) != STATUS_OK)
    {
        for (;;)
        {
        }
    }

    LOG_INFO(MODULE_NAME, "boot");
    LOG_DEBUG(MODULE_NAME, "logger initialised");

    LOG_INFO(MODULE_NAME, "entering main loop");
    uint8_t data_byte = 0;
    frame_t frame     = {0};
    frame.state       = SOF;

    uint8_t serialized_frame[2 + MAX_PAYLOAD] = {0};
    uint8_t serialized_frame_buffer_size      = 2 + MAX_PAYLOAD;
    uint16_t recv_crc                         = 0;

    for (;;)
    {
        while (command_transport_receive(&data_byte) == STATUS_OK)
        {

            frame_results_t frame_status = frame_parser_feed(&frame, data_byte);
            if (frame_status == FRAME_READY)
            {

                int serialized_frame_size = frame_parser_serialize(&frame, serialized_frame, serialized_frame_buffer_size);
                frame_parser_get_crc(&frame, &recv_crc);

                uint16_t crc_computed = crc16_compute(serialized_frame, (uint8_t)serialized_frame_size);
                if (crc16_compare(crc_computed, recv_crc) != true)
                {
                    LOG_INFO(MODULE_NAME, "CRC frame error.");
                    continue;
                }

                LOG_INFO(MODULE_NAME, "Valid frame recieved.");
                response_t resp;
                status_t disp_status = dispatch_command(&frame, &resp);

                if (disp_status != STATUS_OK)
                {
                    LOG_ERROR(MODULE_NAME, "dispatch_command() failed.");
                    continue;
                }

                // TODO: send resp via command_transport_send() once
                // frame_parser_serialize_response() exists (see
                // tests/unit-tests/command_transport_test_list.md).
                LOG_INFO(MODULE_NAME, "Command dispatched.");
            }

            if (frame_status == FRAME_ERROR)
            {
                LOG_ERROR(MODULE_NAME, "Frame Error.");
            }
        }

        logger_flush();
        __WFI();
    }

    return 0;
}