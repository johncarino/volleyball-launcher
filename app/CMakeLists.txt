# Build the app, using the HAL

include_directories(include)
file(GLOB MY_SOURCES "src/*.c")
add_executable(motor_control ${MY_SOURCES})

# Make use of the HAL library
target_link_libraries(motor_control LINK_PRIVATE hal)

# Copy executable to final location
add_custom_command(TARGET motor_control POST_BUILD 
  COMMAND "${CMAKE_COMMAND}" -E make_directory
     "/home/john/Desktop/BeagleY-AI/public"
  COMMAND "${CMAKE_COMMAND}" -E copy 
     "$<TARGET_FILE:motor_control>"
     "/home/john/Desktop/BeagleY-AI/public/motor_control" 
  COMMENT "Copying executable to /home/john/Desktop/BeagleY-AI/public/")
