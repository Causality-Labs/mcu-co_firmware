#ifndef GPIO_H
#define GPIO_H

#include <stdbool.h>
#include "stm32g474xx.h"

#define GPIOA_RESERVED_PINS ((1U << 13U) | (1U << 14U) | (1U << 15U))
#define GPIOB_RESERVED_PINS ((1U << 3U) | (1U << 4U))
#define MAX_PIN_COUNT       15U
#define GPIO_NUM_OF_PORTS   7U
#define MAX_GPIO_INTERRUPTS 16

typedef enum {
    GPIO_PORT_A = 0U,
    GPIO_PORT_B = 1U,
    GPIO_PORT_C = 2U,
    GPIO_PORT_D = 3U,
    GPIO_PORT_E = 4U,
    GPIO_PORT_F = 5U,
    GPIO_PORT_G = 6U,
} gpio_port_t;

typedef struct {
    gpio_port_t port;
    uint8_t pin;
} gpio_pin_t;

typedef enum {
    GPIO_MODE_INPUT  = 0U,
    GPIO_MODE_OUTPUT = 1U,
    GPIO_MODE_AF     = 2U,
    GPIO_MODE_ANALOG = 3U,
} gpio_mode_t;

typedef enum {
    GPIO_TYPE_PUSH_PULL  = 0U,
    GPIO_TYPE_OPEN_DRAIN = 1U,
} gpio_type_t;

typedef enum {
    GPIO_SPEED_LOW       = 0U,
    GPIO_SPEED_MEDIUM    = 1U,
    GPIO_SPEED_HIGH      = 2U,
    GPIO_SPEED_VERY_HIGH = 3U,
} gpio_speed_t;

typedef enum {
    GPIO_PULL_NONE = 0U,
    GPIO_PULL_UP   = 1U,
    GPIO_PULL_DOWN = 2U,
} gpio_pull_t;

typedef enum {
    GPIO_LOW  = 0U,
    GPIO_HIGH = 1U,
} gpio_state_t;

typedef enum {
    RISING  = 0U,
    FALLING = 1U,
    BOTH    = 2U,
} gpio_trigger_t;

typedef enum {
    GPIO_AF0  = 0U,
    GPIO_AF1  = 1U,
    GPIO_AF2  = 2U,
    GPIO_AF3  = 3U,
    GPIO_AF4  = 4U,
    GPIO_AF5  = 5U,
    GPIO_AF6  = 6U,
    GPIO_AF7  = 7U,
    GPIO_AF8  = 8U,
    GPIO_AF9  = 9U,
    GPIO_AF10 = 10U,
    GPIO_AF11 = 11U,
    GPIO_AF12 = 12U,
    GPIO_AF13 = 13U,
    GPIO_AF14 = 14U,
    GPIO_AF15 = 15U,
} gpio_af_t;

typedef void (*gpio_irq_callback_t)(void);

/**
 * @brief GPIO pin configuration parameters.
 */
typedef struct {
    gpio_mode_t mode;
    gpio_type_t type;
    gpio_speed_t speed;
    gpio_pull_t pull;
} gpio_config_t;

/**
 * @brief GPIO external interrupt configuration parameters.
 */
typedef struct {
    gpio_trigger_t trigger;
    gpio_irq_callback_t callback;
    uint8_t priority;
} gpio_irq_config_t;

/**
 * @brief Initialise a GPIO pin.
 *
 * Enables the port clock and configures the pin mode, output type,
 * speed, and pull resistor. Reserved pins (PA13, PA14, PA15, PB3, PB4)
 * are protected and will return an error.
 *
 * @param gpio   Pointer to pin handle (port + pin number)
 * @param config Pointer to pin configuration
 * @return 0 on success, -1 on invalid pin
 */
int gpio_init(const gpio_pin_t *gpio, const gpio_config_t *config);

/**
 * @brief Reset a GPIO pin to its default state.
 *
 * Clears MODER, OTYPER, OSPEEDR, PUPDR, and AFR for the pin.
 * Does not disable the port clock as other pins may still be active.
 *
 * @param gpio Pointer to pin handle (port + pin number)
 * @return 0 on success, -1 on invalid pin
 */
int gpio_deinit(const gpio_pin_t *gpio);

/**
 * @brief Drive a GPIO output pin high.
 *
 * Uses the BSRR register for atomic set.
 *
 * @param gpio Pointer to pin handle (port + pin number)
 * @return 0 on success, -1 if the pin is invalid or not configured as output
 */
int gpio_set(const gpio_pin_t *gpio);

/**
 * @brief Drive a GPIO output pin low.
 *
 * Uses the BRR register for atomic reset.
 *
 * @param gpio Pointer to pin handle (port + pin number)
 * @return 0 on success, -1 if the pin is invalid or not configured as output
 */
int gpio_reset(const gpio_pin_t *gpio);

/**
 * @brief Set a GPIO output pin to an explicit logic level.
 *
 * Calls gpio_set() when @p state is true, gpio_reset() when false.
 *
 * @param gpio  Pointer to pin handle (port + pin number)
 * @param state Desired logic level (true = high, false = low)
 * @return 0 on success, -1 if the pin is invalid or not configured as output
 */
int gpio_set_state(const gpio_pin_t *gpio, gpio_state_t state);

/**
 * @brief Toggle a GPIO output pin.
 *
 * Inverts the current state of the ODR bit for the given pin.
 *
 * @param gpio Pointer to pin handle (port + pin number)
 * @return 0 on success, -1 if the pin is invalid or not configured as output
 */
int gpio_toggle(const gpio_pin_t *gpio);

/**
 * @brief Read the logic level of a GPIO input pin.
 *
 * Samples the IDR register. The pin must be configured as an input.
 *
 * @param gpio  Pointer to pin handle (port + pin number)
 * @param state Output parameter set to true if the pin is high, false if low
 * @return 0 on success, -1 if the pin is invalid or not configured as input
 */
int gpio_read(const gpio_pin_t *gpio, bool *state);

/**
 * @brief Configure a GPIO pin as an external interrupt.
 *
 * Enables the SYSCFG clock, maps the port to the EXTI line, configures the
 * trigger edge, unmasks the line, and enables the NVIC interrupt.
 *
 * @param gpio   Pointer to pin handle (port + pin number)
 * @param config Pointer to interrupt configuration (trigger and callback)
 * @return 0 on success, -1 on invalid pin
 */
int gpio_init_interrupt(const gpio_pin_t *gpio, const gpio_irq_config_t *config);

/**
 * @brief Deconfigure a GPIO external interrupt.
 *
 * Disables the NVIC interrupt, masks the EXTI line, clears the trigger
 * configuration, clears the EXTICR port mapping, and removes the callback.
 *
 * @param gpio Pointer to pin handle (port + pin number)
 * @return 0 on success, -1 on invalid pin
 */
int gpio_deinit_interrupt(const gpio_pin_t *gpio);

/** @brief Returns true if the pin is configured as alternate function. */
bool is_pin_an_af(const gpio_pin_t *gpio);

/** @brief Returns true if the pin is configured as an output. */
bool is_pin_an_output(const gpio_pin_t *gpio);

/** @brief Returns true if the pin is configured as an input. */
bool is_pin_an_input(const gpio_pin_t *gpio);

/**
 * @brief Set the alternate function for a GPIO pin.
 *
 * Writes to AFRL (pins 0–7) or AFRH (pins 8–15). The pin mode must
 * be set to GPIO_MODE_AF separately via gpio_init().
 *
 * @param gpio Pointer to pin handle (port + pin number)
 * @param af   Alternate function selection (AF0–AF15)
 * @return 0 on success, -1 on invalid pin
 */
int gpio_set_af(const gpio_pin_t *gpio, gpio_af_t af);

#endif /* GPIO_H */