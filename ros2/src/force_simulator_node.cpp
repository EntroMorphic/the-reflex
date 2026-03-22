/**
 * force_simulator_node.cpp - Realistic Force Profile Simulator
 * 
 * Simulates a gripper approaching, contacting, and grasping an object.
 * Includes anomaly injection to demonstrate Reflex threshold response.
 * 
 * Phases:
 *   1. APPROACH  (0-2s)   - No force, gripper closing
 *   2. CONTACT   (2-4s)   - Force ramps up as gripper contacts object
 *   3. GRASP     (4-8s)   - Steady force with slight oscillation
 *   4. ANOMALY   (8-9s)   - Sudden force spike (simulates slip or collision)
 *   5. RECOVERY  (9-12s)  - Force returns to safe level
 *   6. RELEASE   (12-14s) - Force decreases as gripper opens
 *   
 *   Repeats every 14 seconds.
 */

#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/wrench_stamped.hpp>
#include <std_msgs/msg/string.hpp>
#include <chrono>
#include <cmath>

using namespace std::chrono_literals;

namespace reflex_ros_bridge {

class ForceSimulatorNode : public rclcpp::Node {
public:
    ForceSimulatorNode() : Node("force_simulator") {
        // Parameters
        declare_parameter("publish_rate_hz", 1000);
        declare_parameter("cycle_duration_s", 14.0);
        declare_parameter("target_force_n", 2.0);
        declare_parameter("anomaly_force_n", 7.0);
        declare_parameter("threshold_force_n", 5.0);
        
        publish_rate_ = get_parameter("publish_rate_hz").as_int();
        cycle_duration_ = get_parameter("cycle_duration_s").as_double();
        target_force_ = get_parameter("target_force_n").as_double();
        anomaly_force_ = get_parameter("anomaly_force_n").as_double();
        threshold_force_ = get_parameter("threshold_force_n").as_double();
        
        // Publishers
        force_pub_ = create_publisher<geometry_msgs::msg::WrenchStamped>(
            "/force_sensor", rclcpp::SensorDataQoS());
        
        phase_pub_ = create_publisher<std_msgs::msg::String>(
            "/force_simulator/phase", 10);
        
        // Timer
        auto period = std::chrono::microseconds(1000000 / publish_rate_);
        timer_ = create_wall_timer(period, [this]() { publish_force(); });
        
        start_time_ = now();
        
        RCLCPP_INFO(get_logger(), "Force simulator started");
        RCLCPP_INFO(get_logger(), "  Publish rate: %d Hz", publish_rate_);
        RCLCPP_INFO(get_logger(), "  Cycle duration: %.1f s", cycle_duration_);
        RCLCPP_INFO(get_logger(), "  Target force: %.1f N", target_force_);
        RCLCPP_INFO(get_logger(), "  Anomaly force: %.1f N (threshold: %.1f N)", 
                    anomaly_force_, threshold_force_);
    }

private:
    void publish_force() {
        auto elapsed = (now() - start_time_).seconds();
        double t = std::fmod(elapsed, cycle_duration_);  // Time within cycle
        
        double force = compute_force(t);
        std::string phase = get_phase(t);
        
        // Publish force
        auto msg = geometry_msgs::msg::WrenchStamped();
        msg.header.stamp = now();
        msg.header.frame_id = "gripper";
        msg.wrench.force.z = force;
        force_pub_->publish(msg);
        
        // Publish phase (for visualization)
        if (phase != last_phase_) {
            auto phase_msg = std_msgs::msg::String();
            phase_msg.data = phase;
            phase_pub_->publish(phase_msg);
            
            RCLCPP_INFO(get_logger(), "Phase: %s (force: %.2f N)", 
                        phase.c_str(), force);
            last_phase_ = phase;
        }
        
        msg_count_++;
    }
    
    double compute_force(double t) {
        // Add slight noise for realism
        double noise = 0.05 * std::sin(t * 47.0) + 0.03 * std::sin(t * 113.0);
        
        if (t < 2.0) {
            // APPROACH: No force
            return 0.0 + noise * 0.1;
        }
        else if (t < 4.0) {
            // CONTACT: Force ramps up (0 to target)
            double progress = (t - 2.0) / 2.0;
            double ramp = smooth_step(progress);
            return target_force_ * ramp + noise;
        }
        else if (t < 8.0) {
            // GRASP: Steady at target with oscillation
            double oscillation = 0.1 * std::sin(t * 10.0);
            return target_force_ + oscillation + noise;
        }
        else if (t < 8.5) {
            // ANOMALY RISING: Sudden spike
            double progress = (t - 8.0) / 0.5;
            return target_force_ + (anomaly_force_ - target_force_) * progress;
        }
        else if (t < 9.0) {
            // ANOMALY PEAK: At dangerous level
            return anomaly_force_ + noise;
        }
        else if (t < 10.0) {
            // RECOVERY: Back to safe
            double progress = (t - 9.0) / 1.0;
            return anomaly_force_ - (anomaly_force_ - target_force_) * smooth_step(progress);
        }
        else if (t < 12.0) {
            // STABLE: Back at target
            double oscillation = 0.1 * std::sin(t * 10.0);
            return target_force_ + oscillation + noise;
        }
        else {
            // RELEASE: Force decreases
            double progress = (t - 12.0) / 2.0;
            return target_force_ * (1.0 - smooth_step(progress)) + noise * 0.1;
        }
    }
    
    std::string get_phase(double t) {
        if (t < 2.0) return "APPROACH";
        if (t < 4.0) return "CONTACT";
        if (t < 8.0) return "GRASP";
        if (t < 9.0) return "ANOMALY";
        if (t < 10.0) return "RECOVERY";
        if (t < 12.0) return "STABLE";
        return "RELEASE";
    }
    
    // Smooth interpolation (ease in/out)
    double smooth_step(double x) {
        x = std::clamp(x, 0.0, 1.0);
        return x * x * (3.0 - 2.0 * x);
    }
    
    // Publishers
    rclcpp::Publisher<geometry_msgs::msg::WrenchStamped>::SharedPtr force_pub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr phase_pub_;
    rclcpp::TimerBase::SharedPtr timer_;
    
    // Parameters
    int publish_rate_;
    double cycle_duration_;
    double target_force_;
    double anomaly_force_;
    double threshold_force_;
    
    // State
    rclcpp::Time start_time_;
    std::string last_phase_;
    uint64_t msg_count_ = 0;
};

}  // namespace reflex_ros_bridge

int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<reflex_ros_bridge::ForceSimulatorNode>();
    
    RCLCPP_INFO(node->get_logger(), 
        "╔═══════════════════════════════════════════════════════════════╗");
    RCLCPP_INFO(node->get_logger(), 
        "║       FORCE SIMULATOR: Grasp Profile Generator                ║");
    RCLCPP_INFO(node->get_logger(), 
        "╚═══════════════════════════════════════════════════════════════╝");
    
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
