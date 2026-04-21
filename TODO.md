# Things to do

## Set up development environment.

Using bare metal STM32 approach via CMSIS, chose to do it this way so I can understand
most of what I am doing. I want to avoid using the STM32 HALs and will focus on using
CMSIS in order to understand as much as possible from the firmware. Will take longer to
setup and will run into a lot of issues but it will be worth it for the experience.

1. Get a toolchain (arm-none-eabi-gcc)
2. Get a build system (CMake + make)
4. Set up VS Code extensions (Cortex Debug)
5. Clone CMSIS core 
6. Clone STM32G4 device headers
6. Write CMake toolchain file and root CMakeLists.txt
7. Configure linker script and startup file for STM32G474RE (Copy what already exist ?)
8. Compile a main.c


## Write a GPIO driver (done)

## Write a UART driver (done)

## Write a logger module that uses UART (doing)

What my logger module needs:
- Severity levels with compile-time filtering (zero cost when disabled)
- Per-module identification so you can tell which service emitted a log
- Safe to call from ISRs (matters for E-Stop and peripheral interrupts)
- Non-blocking at the call site (the logger must never stall a service or an ISR)
- Timestamps captured at the call site, not at drain time (Maybe)
- Works over two transports: ITM/SWO during development, UART in production
- Compile-time-configurable so the entire logger can be stripped from a release build

Logger Module needs a bounded queue data structure and will follow a producer and consumer model.

At the call site we will push the log messge in the bounded queue, then at the drain context we will pop messages out of the queue.

The variying levels of verbosity will allow change the amount of messages that will be loaded onto the queue and decrease the amount of time spent draining the queue.

Must use a ring buffer as the bounded queue. I currently have a ring buffer module but it only works for uint8_t data, I must update this ring buffer to be able to handle any data type. Because we are working C not C++ we will update the current ring buffer iteration to use the `void *` and the `memcpy` design pattern. This design pattern is fine for CERT-C complinace but not MISRA-C, however since I am only going for CERT C this is not an issue.

To make this module safe to call in interrupts, we will have a seperate set of log functions in order to call it from an ISR.

- Refactory Ring Buffer data structure to be generic.
- Make sure it still work with UART.
## Write a clock driver

## Start writng application state machine

