# CMakeList.txt for HAL
#   Build a library (`hal`) which exposes the header files as "hal/*.h"
#   Use header as: #include "hal/pwm.h"

file(GLOB MY_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_library(hal STATIC ${MY_SOURCES})

target_include_directories(hal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link libgpiod (required by stepper driver)
target_link_libraries(hal PUBLIC gpiod)
