# Things to do

## Set up development environment.

Using bare metal STM32 approach via CMSIS, chose to do it this way so I can understand
most of what I am doing. I want to avoid using the STM32 HALs and will focus on using
CMSIS in order to understand as much as possible from the firmware. Will take longer to
setup and will run into a lot of issues but it will be worth it for the experience.

1. Get a toolchain (arm-none-eabi-gcc)
2. Get a build system (CMake + Ninja)
4. Set up VS Code extensions (Cortex Debug)
5. Clone CMSIS core 
6. Clone STM32G4 device headers
6. Write CMake toolchain file and root CMakeLists.txt
7. Configure linker script and startup file for STM32G474RE (Copy what already exist ?)
8. Compile a main.c


## Write a GPIO driver

## Write a UART driver

## Write a script for flashing the firmware

