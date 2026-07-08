#ifndef STATUS_H
#define STATUS_H

/**
 * @file status.h
 * @brief Project-wide return status codes shared by all drivers and layers.
 */

/**
 * @brief Unified status / error code returned across the firmware.
 */
typedef enum {
    STATUS_OK = 0,            /**< Operation completed successfully.            */
    STATUS_ERR_INVALID_ARG,   /**< NULL pointer or out-of-range enum argument.  */
    STATUS_ERR_INVALID_PIN,   /**< Pin number out of range or a reserved pin.   */
    STATUS_ERR_INVALID_STATE, /**< Resource not configured for this operation.  */
    STATUS_ERR_NOT_INIT,      /**< Peripheral or clock not initialised yet.     */
    STATUS_ERR_BUSY,          /**< Resource already initialised or in use.      */
    STATUS_ERR_TIMEOUT,       /**< Hardware did not respond within the timeout. */
    STATUS_ERR_UNSUPPORTED,   /**< Valid request the driver cannot satisfy.     */
    STATUS_ERR_EMPTY,         /**< No data available (e.g. RX buffer empty).    */
    STATUS_ERR_FULL,          /**< No space available (e.g. TX/ring buffer full).*/
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
