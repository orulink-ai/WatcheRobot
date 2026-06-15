set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# CMake running on Windows can cache host system libraries such as kernel32
# when a build directory has previously been configured with a native toolchain.
# Bare-metal arm-none-eabi links must not receive those libraries.
set(CMAKE_C_STANDARD_LIBRARIES_INIT "")
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "")
set(CMAKE_C_STANDARD_LIBRARIES "" CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD_LIBRARIES "" CACHE STRING "" FORCE)

set(TOOLCHAIN_PREFIX arm-none-eabi-)
set(TOOLCHAIN_SEARCH_PATHS
    "C:/Users/25772/AppData/Local/stm32cube/bundles/gnu-tools-for-stm32/14.3.1+st.2/bin"
    "C:/Users/25772/Tools/ArmGnuToolchain/14.2.Rel1/bin"
    "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/13.2 Rel1/bin"
    "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 Rel1/bin"
    "C:/Program Files/Arm GNU Toolchain arm-none-eabi/14.2 Rel1/bin"
    /opt/homebrew/bin
    /usr/local/bin
)

find_program(CMAKE_C_COMPILER NAMES ${TOOLCHAIN_PREFIX}gcc PATHS ${TOOLCHAIN_SEARCH_PATHS} REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES ${TOOLCHAIN_PREFIX}g++ PATHS ${TOOLCHAIN_SEARCH_PATHS} REQUIRED)
find_program(CMAKE_ASM_COMPILER NAMES ${TOOLCHAIN_PREFIX}gcc PATHS ${TOOLCHAIN_SEARCH_PATHS} REQUIRED)
find_program(CMAKE_OBJCOPY NAMES ${TOOLCHAIN_PREFIX}objcopy PATHS ${TOOLCHAIN_SEARCH_PATHS} REQUIRED)
find_program(CMAKE_SIZE NAMES ${TOOLCHAIN_PREFIX}size PATHS ${TOOLCHAIN_SEARCH_PATHS} REQUIRED)

set(CMAKE_EXECUTABLE_SUFFIX ".elf")

set(MCU_FLAGS "-mcpu=cortex-m3 -mthumb")
set(COMMON_FLAGS "${MCU_FLAGS} -ffunction-sections -fdata-sections")

set(CMAKE_C_FLAGS_INIT "${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${COMMON_FLAGS} -fno-exceptions -fno-rtti")
set(CMAKE_ASM_FLAGS_INIT "${COMMON_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${MCU_FLAGS} -specs=nano.specs -specs=nosys.specs")
