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
add_executable(motor_control src/main.c ${SHARED_SOURCES})
target_link_libraries(motor_control PRIVATE hal m)

# ---- Function tests (app/functiontests/) ----

# BTS7960 H-bridge test program
add_executable(bts7960_test functiontests/bts7960_test_main.c)
target_link_libraries(bts7960_test PRIVATE hal)

# TB6600 stepper motor test program
add_executable(tb6600_test functiontests/tb6600_test_main.c)
target_link_libraries(tb6600_test PRIVATE hal gpiod)

# MCP4725 DAC test program
add_executable(mcp4725_test functiontests/mcp4725_test_main.c)
target_link_libraries(mcp4725_test PRIVATE hal)

# Full system integration test (BTS7960 + MCP4725 + TB6600)
add_executable(system_integration_test functiontests/system_integration_test_main.c)
target_link_libraries(system_integration_test PRIVATE hal gpiod)

# FSM Test Program
add_executable(fsm_test functiontests/fsm_test.c ${SHARED_SOURCES})
target_link_libraries(fsm_test PRIVATE hal m)

# Copy executables to final location, and register copies for make clean
foreach(target motor_control bts7960_test tb6600_test mcp4725_test system_integration_test fsm_test)
  add_custom_command(TARGET ${target} POST_BUILD 
    COMMAND "${CMAKE_COMMAND}" -E make_directory   
       "/home/john/Desktop/BeagleY-AI/public"
    COMMAND "${CMAKE_COMMAND}" -E copy 
       "$<TARGET_FILE:${target}>"
       "/home/john/Desktop/BeagleY-AI/public/${target}" 
    COMMENT "Copying ${target} to /home/john/Desktop/BeagleY-AI/public/")
  set_property(DIRECTORY APPEND PROPERTY
    ADDITIONAL_MAKE_CLEAN_FILES
    "/home/john/Desktop/BeagleY-AI/public/${target}")
endforeach()
