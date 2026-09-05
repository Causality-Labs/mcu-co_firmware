#ifndef STATUS_H
#define STATUS_H

/**
 * @file status.h
 * @brief Project-wide return status codes shared by all drivers and layers.
 */

/**
 * @brief Unified status / error code returned across the firmware.
 *
 * These values travel on the wire: a NACK carries the failing status as its
 * single data byte (see the response frame in mcu-co_Protocol.md). Values are
 * therefore fixed - append new codes at the end, never renumber or insert.
 */
typedef enum
{
    STATUS_OK                = 0,  /**< Operation completed successfully.            */
    STATUS_ERR               = 1,  /**< Generic/unspecified failure.                 */
    STATUS_ERR_INVALID_ARG   = 2,  /**< NULL pointer or out-of-range enum argument.  */
    STATUS_ERR_INVALID_PIN   = 3,  /**< Pin number out of range or a reserved pin.   */
    STATUS_ERR_INVALID_STATE = 4,  /**< Resource not configured for this operation.  */
    STATUS_ERR_NOT_INIT      = 5,  /**< Peripheral or clock not initialised yet.     */
    STATUS_ERR_BUSY          = 6,  /**< Resource already initialised or in use.      */
    STATUS_ERR_TIMEOUT       = 7,  /**< Hardware did not respond within the timeout. */
    STATUS_ERR_UNSUPPORTED   = 8,  /**< Valid request the driver cannot satisfy.     */
    STATUS_ERR_EMPTY         = 9,  /**< No data available (e.g. RX buffer empty).    */
    STATUS_ERR_FULL          = 10, /**< No space available (e.g. TX/ring buffer full).*/
} status_t;

/**
 * @brief Return a human-readable name for a status code.
 *
 * Intended for logging. Always returns a valid, non-NULL string; unknown
 * values map to "?".
 *
 * @param status Status code to describe.
 * @return Constant string naming the status code.
 */
const char *status_to_str(status_t status);

#endif /* STATUS_H */
