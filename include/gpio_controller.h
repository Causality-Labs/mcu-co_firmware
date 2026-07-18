#ifndef GPIO_CONTROLLER_H
#define GPIO_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>
#include "status.h"

status_t gpio_controller_cfg(const uint8_t *payload, uint8_t length);
status_t gpio_controller_write(const uint8_t *payload, uint8_t length);
status_t gpio_controller_read(const uint8_t *payload, uint8_t length, bool *state);
status_t gpio_controller_irq(const uint8_t *payload, uint8_t length);

#endif /* GPIO_CONTROLLER_H */