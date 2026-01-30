#include <rclcpp/rclcpp.hpp>
#include <memory>

// Forward declaration
std::shared_ptr<rclcpp::Node> create_bridge_node();

int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);
    
    auto node = create_bridge_node();
    
    RCLCPP_INFO(node->get_logger(), 
        "╔═══════════════════════════════════════════════════════════════╗");
    RCLCPP_INFO(node->get_logger(), 
        "║       REFLEX ROS BRIDGE: Sub-Microsecond Control              ║");
    RCLCPP_INFO(node->get_logger(), 
        "╚═══════════════════════════════════════════════════════════════╝");
    
    rclcpp::spin(node);
    rclcpp::shutdown();
    
    return 0;
}
