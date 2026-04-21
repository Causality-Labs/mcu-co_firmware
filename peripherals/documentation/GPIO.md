# GPIO information for developing the drivers

## Registers

Each GPIO port exposes the following 32-bit registers:

| Register | Type | Purpose |
|---|---|---|
| `GPIOx_MODER` | Config | Pin mode (input / output / alternate function / analog) |
| `GPIOx_OTYPER` | Config | Output type (push-pull / open-drain) |
| `GPIOx_OSPEEDR` | Config | Output speed |
| `GPIOx_PUPDR` | Config | Pull-up / pull-down |
| `GPIOx_IDR` | Data | Input data (read pin states) |
| `GPIOx_ODR` | Data | Output data (write pin states) |
| `GPIOx_BSRR` | Set/Reset | Atomic bit set and reset |
| `GPIOx_LCKR` | Lock | Lock configuration registers |
| `GPIOx_AFRL` | Alt function | Alternate function selection for pins 0–7 |
| `GPIOx_AFRH` | Alt function | Alternate function selection for pins 8–15 |
