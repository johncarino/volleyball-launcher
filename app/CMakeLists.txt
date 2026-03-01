# Build the app, using the HAL

include_directories(include)
file(GLOB MY_SOURCES "src/*.c")
add_executable(hello_world ${MY_SOURCES})

# Make use of the HAL library
target_link_libraries(hello_world LINK_PRIVATE hal)

# Copy executable to final location (change `hello_world` to project name as needed)
add_custom_command(TARGET hello_world POST_BUILD 
  COMMAND "${CMAKE_COMMAND}" -E make_directory
     "/home/john/Desktop/BeagleY-AI/public"
  COMMAND "${CMAKE_COMMAND}" -E copy 
     "$<TARGET_FILE:hello_world>"
     "/home/john/Desktop/BeagleY-AI/public/hello_world" 
  COMMENT "Copying executable to /home/john/Desktop/BeagleY-AI/public/")
