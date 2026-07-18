#ifndef COMMAND_DISPATCHER_H
#define COMMAND_DISPATCHER_H

#include <stdint.h>
#include "status.h"
#include "frame_parser.h"

typedef struct
{
    bool ack;
    uint8_t state;
} response_t;

status_t dispatch_command(frame_t *frame, response_t *resp);

#endif /* COMMAND_DISPATCHER_H */