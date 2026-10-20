# CMake toolchain file for cross-compiling to ARM 32-bit (armhf / R36S / RK3326)
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(ARM_TRIPLE "arm-linux-gnueabihf")
find_program(CMAKE_C_COMPILER NAMES ${ARM_TRIPLE}-gcc REQUIRED)
find_program(CMAKE_OBJCOPY NAMES ${ARM_TRIPLE}-objcopy REQUIRED)
find_program(CMAKE_OBJDUMP NAMES ${ARM_TRIPLE}-objdump REQUIRED)
find_program(CMAKE_NM NAMES ${ARM_TRIPLE}-nm REQUIRED)
find_program(CMAKE_STRIP NAMES ${ARM_TRIPLE}-strip REQUIRED)

set(CMAKE_MAKE_PROGRAM /usr/bin/ninja CACHE FILEPATH "" FORCE)
set(PSXSTACK_OBJCOPY "${CMAKE_OBJCOPY}" CACHE FILEPATH "" FORCE)
set(PSXSTACK_OBJDUMP "${CMAKE_OBJDUMP}" CACHE FILEPATH "" FORCE)

# The handhelds these builds run on are Cortex-A35 (RK3326 and its siblings): an ARMv8-A core in AArch32 mode,
# which has SDIV/UDIV in hardware and the dual-issue scheduling of that core. Targeting armv7-a instead left every
# integer division a call to __aeabi_idiv (30-50 cycles), and the GPU's per-span setup divides: gpu.c had 17 such
# call sites. -marm stays explicit (Thumb-2 would be a different codegen to re-validate).
set(CMAKE_C_FLAGS_INIT "-marm -mcpu=cortex-a35 -mfpu=neon-fp-armv8 -mfloat-abi=hard")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
