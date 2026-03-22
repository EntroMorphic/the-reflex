from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration

def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument(
            'force_topic',
            default_value='/force_sensor',
            description='Force sensor topic'
        ),
        DeclareLaunchArgument(
            'command_topic', 
            default_value='/gripper_command',
            description='Gripper command topic'
        ),
        DeclareLaunchArgument(
            'poll_rate_hz',
            default_value='1000',
            description='Bridge poll rate in Hz'
        ),
        
        # Bridge node
        Node(
            package='reflex_ros_bridge',
            executable='bridge_node',
            name='reflex_bridge',
            parameters=[{
                'force_topic': LaunchConfiguration('force_topic'),
                'command_topic': LaunchConfiguration('command_topic'),
                'poll_rate_hz': LaunchConfiguration('poll_rate_hz'),
            }],
            output='screen'
        ),
        
        # Telemetry node
        Node(
            package='reflex_ros_bridge',
            executable='telemetry_node',
            name='reflex_telemetry',
            output='screen'
        ),
    ])
