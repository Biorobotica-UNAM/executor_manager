cmake_minimum_required(VERSION 3.8)
project(task_manager)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()


find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)


add_executable(instruction_parser src/instruction_parser.cpp)
ament_target_dependencies(instruction_parser rclcpp std_msgs)


install(TARGETS
  instruction_parser
  DESTINATION lib/${PROJECT_NAME})

ament_package()