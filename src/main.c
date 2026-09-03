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

// const gpio_pin_t led                            = {.port = GPIO_PORT_A, .pin = (uint8_t)5U};
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
    uint8_t data_byte     = 0;
    command_frame_t frame = {0};
    frame.state           = SOF;

    uint8_t serialized_frame[2 + RX_MAX_PAYLOAD] = {0};
    uint8_t serialized_frame_buffer_size         = 2 + RX_MAX_PAYLOAD;
    uint16_t recv_crc                            = 0;

    uint8_t response_frame[TX_FRAME_MAX] = {0};

    for (;;)
    {
        while (command_transport_receive(&data_byte) == STATUS_OK)
        {

            frame_results_t frame_status = frame_parser_feed(&frame, data_byte);
            if (frame_status == FRAME_READY)
            {

                int serialized_frame_size = frame_parser_serialize(&frame, serialized_frame, serialized_frame_buffer_size);
                if (serialized_frame_size < 0)
                {
                    LOG_ERROR(MODULE_NAME, "frame_parser_serialize() failed (%d)", serialized_frame_size);
                    continue;
                }

                if (frame_parser_get_crc(&frame, &recv_crc) < 0)
                {
                    LOG_ERROR(MODULE_NAME, "frame_parser_get_crc() failed.");
                    continue;
                }

                uint16_t crc_computed = crc16_compute(serialized_frame, (uint8_t)serialized_frame_size);
                if (crc16_compare(crc_computed, recv_crc) != true)
                {
                    LOG_INFO(MODULE_NAME, "CRC error: computed 0x%04x, received 0x%04x", crc_computed, recv_crc);
                    continue;
                }

                LOG_INFO(MODULE_NAME, "valid frame received: opcode 0x%02x, len %u", frame.opcode, frame.length);

                /* {0} leaves ack false, so this is already a valid NACK if
                 * dispatch_command() returns before populating it. */
                response_frame_t resp = {0};
                status_t disp_status  = dispatch_command(&frame, &resp);

                if (disp_status != STATUS_OK)
                {
                    /* No continue: the host is waiting on a reply, and resp is
                     * a populated NACK on every dispatch failure path. */
                    LOG_ERROR(MODULE_NAME, "dispatch_command() failed (%s), sending NACK", status_to_str(disp_status));
                }

                int response_len = frame_parser_serialize_response(&resp, response_frame, sizeof(response_frame));
                if (response_len < 0)
                {
                    LOG_ERROR(MODULE_NAME, "frame_parser_serialize_response() failed (%d)", response_len);
                    continue;
                }

                /* CRC covers LEN onward - SOF is excluded. The RX path needs no
                 * such offset because frame_parser_serialize() returns only the
                 * covered bytes. */
                uint16_t resp_crc = crc16_compute(&response_frame[TX_LEN_IDX], (uint8_t)(response_len - TX_LEN_IDX));

                response_len = frame_parser_append_crc(response_frame, (uint8_t)response_len,
                                                       (uint8_t)sizeof(response_frame), resp_crc);
                if (response_len < 0)
                {
                    LOG_ERROR(MODULE_NAME, "frame_parser_append_crc() failed (%d)", response_len);
                    continue;
                }

                status_t send_status = command_transport_send(response_frame, (uint16_t)response_len);
                if (send_status != STATUS_OK)
                {
                    LOG_ERROR(MODULE_NAME, "command_transport_send() failed: %s", status_to_str(send_status));
                }
            }

            if (frame_status == FRAME_ERROR)
            {
                LOG_ERROR(MODULE_NAME, "frame error on byte 0x%02x", data_byte);
            }
        }

        logger_flush();
        __WFI();
    }

    return 0;
}