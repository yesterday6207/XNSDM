from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    return LaunchDescription(
        [
            DeclareLaunchArgument("team", default_value="A"),
            DeclareLaunchArgument("team_name", default_value="RMTC-Bot"),
            DeclareLaunchArgument("config_dir", default_value=""),
            DeclareLaunchArgument("stage", default_value="1"),
            Node(
                package="rmtc_bot",
                executable="main_node",
                name="rmtc_bot",
                output="screen",
                parameters=[
                    {
                        "team": LaunchConfiguration("team"),
                        "team_name": LaunchConfiguration("team_name"),
                        "config_dir": LaunchConfiguration("config_dir"),
                        "stage": ParameterValue(
                            LaunchConfiguration("stage"), value_type=int
                        ),
                    }
                ],
            )
        ]
    )
