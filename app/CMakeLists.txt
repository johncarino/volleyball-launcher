# Build the app, using the HAL

include_directories(src/include)

# Shared (non-main) source files
set(SHARED_SOURCES
    src/advanced.c
    src/arc_calc.c
    src/calibration.c
    src/fsm.c
    src/operation.c
    src/set.c
)

# Main application (FSM-based)
add_executable(motor_control src/fsm_main.c ${SHARED_SOURCES})
target_link_libraries(motor_control PRIVATE hal m)

# BTS7960 test program
add_executable(bts_test src/main.c)
target_link_libraries(bts_test PRIVATE hal)

# PWM motor test program
add_executable(motor_test src/motor_test_main.c)
target_link_libraries(motor_test PRIVATE hal)

# Copy executables to final location
foreach(target motor_control bts_test motor_test)
  add_custom_command(TARGET ${target} POST_BUILD 
    COMMAND "${CMAKE_COMMAND}" -E make_directory
       "/home/john/Desktop/BeagleY-AI/public"
    COMMAND "${CMAKE_COMMAND}" -E copy 
       "$<TARGET_FILE:${target}>"
       "/home/john/Desktop/BeagleY-AI/public/${target}" 
    COMMENT "Copying ${target} to /home/john/Desktop/BeagleY-AI/public/")
endforeach()
