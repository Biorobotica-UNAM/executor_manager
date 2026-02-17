import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node

def generate_launch_description():
    
    # Argumento para saber si estamos simulando (por si quieres cambiar parámetros luego)
    sim_mode = LaunchConfiguration('sim', default='false')

    return LaunchDescription([
        
        DeclareLaunchArgument(
            'sim',
            default_value='false',
            description='Set to true if running in simulation'),

        # 1. EXECUTOR MANAGER 
        Node(
            package='executor_manager',
            executable='executor_manager_node',
            name='executor_manager_node',
            output='screen',
            emulate_tty=True
        ),

        # 2. EXECUTOR WORKER 
        Node(
            package='executor_worker',
            executable='executor_worker_node',
            name='executor_worker_node',
            output='screen',
            emulate_tty=True
        ),

        # 3. SUPERVISOR PLAN 
        Node(
            package='supervisor_plan',
            executable='supervisor_plan_node', 
            name='supervisor_plan_node',
            output='screen',
            emulate_tty=True
        ),
    ])