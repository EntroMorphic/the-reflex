cmake_minimum_required(VERSION 3.8)
project(reflex_ros_bridge)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -O3)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)

include_directories(include)

# Shared memory channel library
add_library(reflex_channel SHARED
  src/channel.cpp
)
target_link_libraries(reflex_channel rt)

# Bridge node
add_executable(bridge_node
  src/bridge_node.cpp
  src/main.cpp
)
ament_target_dependencies(bridge_node
  rclcpp
  std_msgs
  geometry_msgs
  sensor_msgs
)
target_link_libraries(bridge_node reflex_channel)

# Telemetry node
add_executable(telemetry_node
  src/telemetry_node.cpp
)
ament_target_dependencies(telemetry_node
  rclcpp
  std_msgs
)
target_link_libraries(telemetry_node reflex_channel)

# Install executables
install(TARGETS
  bridge_node
  telemetry_node
  DESTINATION lib/${PROJECT_NAME}
)

# Install library to lib directory (for LD_LIBRARY_PATH)
install(TARGETS reflex_channel
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
)

install(DIRECTORY include/
  DESTINATION include
)

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
