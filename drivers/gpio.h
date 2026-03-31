#include <stdbool.h>
#include "stm32g474xx.h"

#define GPIOA_RESERVED_PINS  ((1U << 13) | (1U << 14) | (1U << 15))
#define GPIOB_RESERVED_PINS  ((1U << 3)  | (1U << 4))
#define MAX_PIN_COUNT 15

/**
 * @brief GPIO pin configuration parameters.
 *
 * @var gpio_config_t::mode   Pin mode (0=input, 1=output, 2=alternate function, 3=analog)
 * @var gpio_config_t::type   Output type (0=push-pull, 1=open-drain)
 * @var gpio_config_t::speed  Output speed (0=low, 1=medium, 2=high, 3=very high)
 * @var gpio_config_t::pull   Pull resistor (0=none, 1=pull-up, 2=pull-down)
 */
typedef struct {
    uint8_t mode;
    uint8_t type;
    uint8_t speed;
    uint8_t pull;
} gpio_config_t;

/**
 * @brief Initialise a GPIO pin.
 *
 * Enables the port clock and configures the pin mode, output type,
 * speed, and pull resistor. Reserved pins (PA13, PA14) are protected
 * and will return an error.
 *
 * @param port   GPIO port (e.g. GPIOA, GPIOB)
 * @param pin    Pin number (0-15)
 * @param config Pointer to pin configuration
 * @return 0 on success, -1 if the pin is reserved
 */
int gpio_init(GPIO_TypeDef *port, uint8_t pin, gpio_config_t *config);
int gpio_set(GPIO_TypeDef *port, uint8_t pin);
int gpio_reset(GPIO_TypeDef *port, uint8_t pin);
int gpio_set_state(GPIO_TypeDef *port, uint8_t pin, bool state);
int gpio_toggle(GPIO_TypeDef *port, uint8_t pin);
int gpio_read(GPIO_TypeDef *port, uint8_t pin, bool *state);