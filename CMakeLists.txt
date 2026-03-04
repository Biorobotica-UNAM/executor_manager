cmake_minimum_required(VERSION 3.8)
project(executor_manager)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(executor_interfaces REQUIRED) 

add_executable(executor_manager_node src/executor_manager_node.cpp)

ament_target_dependencies(executor_manager_node 
  rclcpp 
  std_msgs
  std_srvs
  rclcpp_action
  executor_interfaces
)

install(TARGETS
  executor_manager_node
  DESTINATION lib/${PROJECT_NAME})
  
install(
  DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
