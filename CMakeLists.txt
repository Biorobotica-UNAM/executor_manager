cmake_minimum_required(VERSION 3.8)
project(executor_manager)

# -----------------------------
# Dependencies
# -----------------------------
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(rclcpp_action REQUIRED)

find_package(executor_interfaces REQUIRED) 
find_package(supervisor_executor_interfaces REQUIRED)

# -----------------------------
# Executable
# -----------------------------
add_executable(executor_manager_node src/executor_manager_node.cpp)

ament_target_dependencies(executor_manager_node 
  rclcpp
  rclcpp_action
  std_msgs
  std_srvs
  executor_interfaces
  supervisor_executor_interfaces
)

# -----------------------------
# Install
# -----------------------------
install(
  TARGETS executor_manager_node
  DESTINATION lib/${PROJECT_NAME}
)
  
install(
  DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
