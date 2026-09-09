#ifndef COMMAND_DISPATCHER_H
#define COMMAND_DISPATCHER_H

#include <stdint.h>
#include "status.h"
#include "frame_parser.h"

/**
 * @brief Route a CRC-checked command frame to its controller and build the reply.
 *
 * On success @p resp is an ACK carrying a read opcode's value, little-endian, in
 * the width the protocol fixes for it; on failure a NACK whose single data byte
 * is the failing ::status_t. Either way @p resp is populated once the opcode has
 * been looked up, so a caller may send it without checking the return value -
 * only the two NULL rejections leave it untouched.
 *
 * @param frame Parsed command frame, its CRC already verified
 * @param resp  Output parameter for the reply to send
 * @return STATUS_OK on success, STATUS_ERR_INVALID_ARG if @p frame or @p resp
 *         is NULL, STATUS_ERR_UNSUPPORTED if no command owns the opcode, or the
 *         status the controller failed with.
 */
status_t dispatch_command(command_frame_t *frame, response_frame_t *resp);

#endif /* COMMAND_DISPATCHER_H */
