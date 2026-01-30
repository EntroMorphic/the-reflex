#include "reflex_ros_bridge/channel.hpp"

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float64.hpp>
#include <geometry_msgs/msg/wrench_stamped.hpp>
#include <chrono>
#include <memory>

using namespace std::chrono_literals;

namespace reflex_ros_bridge {

class BridgeNode : public rclcpp::Node {
public:
    BridgeNode() : Node("reflex_bridge") {
        // Declare parameters
        declare_parameter("force_topic", "/force_sensor");
        declare_parameter("command_topic", "/gripper_command");
        declare_parameter("poll_rate_hz", 1000);
        
        auto force_topic = get_parameter("force_topic").as_string();
        auto command_topic = get_parameter("command_topic").as_string();
        auto poll_rate = get_parameter("poll_rate_hz").as_int();
        
        RCLCPP_INFO(get_logger(), "Initializing Reflex bridge...");
        RCLCPP_INFO(get_logger(), "  Force topic: %s", force_topic.c_str());
        RCLCPP_INFO(get_logger(), "  Command topic: %s", command_topic.c_str());
        RCLCPP_INFO(get_logger(), "  Poll rate: %ld Hz", poll_rate);
        
        // Create shared memory channels
        try {
            force_channel_ = std::make_unique<SharedChannel>("reflex_force", true);
            command_channel_ = std::make_unique<SharedChannel>("reflex_command", true);
            telemetry_channel_ = std::make_unique<SharedChannel>("reflex_telemetry", true);
            RCLCPP_INFO(get_logger(), "Shared memory channels created");
        } catch (const std::exception& e) {
            RCLCPP_ERROR(get_logger(), "Failed to create channels: %s", e.what());
            throw;
        }
        
        // ROS2 → Reflex: Force sensor subscription
        force_sub_ = create_subscription<geometry_msgs::msg::WrenchStamped>(
            force_topic, rclcpp::SensorDataQoS(),
            [this](const geometry_msgs::msg::WrenchStamped::SharedPtr msg) {
                on_force_received(msg);
            });
        
        // Reflex → ROS2: Command publisher
        command_pub_ = create_publisher<std_msgs::msg::Float64>(
            command_topic, rclcpp::SensorDataQoS());
        
        // Telemetry publisher
        latency_pub_ = create_publisher<std_msgs::msg::Float64>(
            "/reflex/latency_ns", 10);
        anomaly_pub_ = create_publisher<std_msgs::msg::Float64>(
            "/reflex/anomaly", 10);
        
        // Timer to poll command channel from Reflex
        auto period = std::chrono::microseconds(1000000 / poll_rate);
        timer_ = create_wall_timer(period, [this]() { poll_reflex(); });
        
        RCLCPP_INFO(get_logger(), "Reflex bridge initialized");
    }

private:
    void on_force_received(const geometry_msgs::msg::WrenchStamped::SharedPtr msg) {
        // Calculate force magnitude
        double fx = msg->wrench.force.x;
        double fy = msg->wrench.force.y;
        double fz = msg->wrench.force.z;
        double force = std::sqrt(fx*fx + fy*fy + fz*fz);
        
        // Signal to Reflex channel
        force_channel_->signal(encode_force(force));
        
        force_count_++;
    }
    
    void poll_reflex() {
        // Check for new command from Reflex
        uint64_t seq = command_channel_->sequence();
        if (seq != last_command_seq_) {
            last_command_seq_ = seq;
            
            // Publish command to ROS2
            auto msg = std_msgs::msg::Float64();
            msg.data = decode_position(command_channel_->read());
            command_pub_->publish(msg);
            
            command_count_++;
        }
        
        // Check telemetry
        uint64_t telem_seq = telemetry_channel_->sequence();
        if (telem_seq != last_telemetry_seq_) {
            last_telemetry_seq_ = telem_seq;
            
            uint64_t value = telemetry_channel_->read();
            
            // Publish anomaly status
            auto anomaly_msg = std_msgs::msg::Float64();
            anomaly_msg.data = static_cast<double>(value);
            anomaly_pub_->publish(anomaly_msg);
        }
        
        // Periodic stats
        poll_count_++;
        if (poll_count_ % 1000 == 0) {
            RCLCPP_DEBUG(get_logger(), 
                "Bridge stats: force_msgs=%lu, command_msgs=%lu",
                force_count_, command_count_);
        }
    }
    
    // Channels
    std::unique_ptr<SharedChannel> force_channel_;
    std::unique_ptr<SharedChannel> command_channel_;
    std::unique_ptr<SharedChannel> telemetry_channel_;
    
    // ROS2 interfaces
    rclcpp::Subscription<geometry_msgs::msg::WrenchStamped>::SharedPtr force_sub_;
    rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr command_pub_;
    rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr latency_pub_;
    rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr anomaly_pub_;
    rclcpp::TimerBase::SharedPtr timer_;
    
    // State
    uint64_t last_command_seq_ = 0;
    uint64_t last_telemetry_seq_ = 0;
    uint64_t force_count_ = 0;
    uint64_t command_count_ = 0;
    uint64_t poll_count_ = 0;
};

}  // namespace reflex_ros_bridge

// Expose for main.cpp
std::shared_ptr<rclcpp::Node> create_bridge_node() {
    return std::make_shared<reflex_ros_bridge::BridgeNode>();
}
