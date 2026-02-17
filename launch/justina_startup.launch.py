import os
from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([

        # 1. EXECUTOR MANAGER 
        Node(
            package='executor_manager',
            executable='executor_manager_node',
            name='executor_manager_node',
            output='screen'
        ),

        # 2. EXECUTOR WORKER 
        Node(
            package='executor_worker',
            executable='executor_worker_node',
            name='executor_worker_node',
            output='screen'
        ),

        # 3. SUPERVISOR PLAN 
        Node(
            package='supervisor_plan', 
            executable='supervisor_plan_node',
            name='supervisor_plan_node',
            output='screen'
        ),
    ])