#include "reflex_ros_bridge/channel.hpp"

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float64.hpp>
#include <chrono>
#include <deque>
#include <algorithm>
#include <numeric>

using namespace std::chrono_literals;

namespace reflex_ros_bridge {

class TelemetryNode : public rclcpp::Node {
public:
    TelemetryNode() : Node("reflex_telemetry") {
        RCLCPP_INFO(get_logger(), "Initializing Reflex telemetry...");
        
        // Open channels (created by bridge_node)
        try {
            force_channel_ = std::make_unique<SharedChannel>("reflex_force", false);
            command_channel_ = std::make_unique<SharedChannel>("reflex_command", false);
            telemetry_channel_ = std::make_unique<SharedChannel>("reflex_telemetry", false);
            RCLCPP_INFO(get_logger(), "Connected to shared memory channels");
        } catch (const std::exception& e) {
            RCLCPP_ERROR(get_logger(), "Failed to open channels: %s", e.what());
            throw;
        }
        
        // Publishers for visualization
        force_pub_ = create_publisher<std_msgs::msg::Float64>("/reflex/force", 10);
        command_pub_ = create_publisher<std_msgs::msg::Float64>("/reflex/command", 10);
        rate_pub_ = create_publisher<std_msgs::msg::Float64>("/reflex/rate_hz", 10);
        latency_median_pub_ = create_publisher<std_msgs::msg::Float64>("/reflex/latency_median_ns", 10);
        latency_p99_pub_ = create_publisher<std_msgs::msg::Float64>("/reflex/latency_p99_ns", 10);
        
        // High-frequency poll (10kHz)
        timer_ = create_wall_timer(100us, [this]() { poll(); });
        
        // Stats publisher (1Hz)
        stats_timer_ = create_wall_timer(1s, [this]() { publish_stats(); });
        
        RCLCPP_INFO(get_logger(), "Reflex telemetry started");
    }

private:
    void poll() {
        auto now = std::chrono::steady_clock::now();
        
        // Check for new telemetry
        uint64_t seq = telemetry_channel_->sequence();
        if (seq != last_seq_) {
            last_seq_ = seq;
            
            // Get values
            double force = decode_force(force_channel_->read());
            double command = decode_position(command_channel_->read());
            
            // Publish
            auto force_msg = std_msgs::msg::Float64();
            force_msg.data = force;
            force_pub_->publish(force_msg);
            
            auto cmd_msg = std_msgs::msg::Float64();
            cmd_msg.data = command;
            command_pub_->publish(cmd_msg);
            
            // Calculate latency (time since telemetry was signaled)
            uint64_t telem_ts = telemetry_channel_->timestamp();
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
            uint64_t now_ns = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
            
            if (telem_ts > 0 && now_ns > telem_ts) {
                uint64_t latency = now_ns - telem_ts;
                latencies_.push_back(latency);
                if (latencies_.size() > 10000) {
                    latencies_.pop_front();
                }
            }
            
            sample_count_++;
        }
        
        // Track timing for rate calculation
        if (last_poll_time_.time_since_epoch().count() > 0) {
            auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_poll_time_);
            poll_intervals_.push_back(dt.count());
            if (poll_intervals_.size() > 10000) {
                poll_intervals_.pop_front();
            }
        }
        last_poll_time_ = now;
    }
    
    void publish_stats() {
        // Calculate rate
        if (!poll_intervals_.empty()) {
            double avg_interval_ns = std::accumulate(
                poll_intervals_.begin(), poll_intervals_.end(), 0.0) / poll_intervals_.size();
            double rate_hz = 1e9 / avg_interval_ns;
            
            auto rate_msg = std_msgs::msg::Float64();
            rate_msg.data = rate_hz;
            rate_pub_->publish(rate_msg);
        }
        
        // Calculate latency stats
        if (!latencies_.empty()) {
            std::vector<uint64_t> sorted(latencies_.begin(), latencies_.end());
            std::sort(sorted.begin(), sorted.end());
            
            size_t n = sorted.size();
            uint64_t median = sorted[n / 2];
            uint64_t p99 = sorted[n * 99 / 100];
            
            auto median_msg = std_msgs::msg::Float64();
            median_msg.data = static_cast<double>(median);
            latency_median_pub_->publish(median_msg);
            
            auto p99_msg = std_msgs::msg::Float64();
            p99_msg.data = static_cast<double>(p99);
            latency_p99_pub_->publish(p99_msg);
            
            RCLCPP_INFO(get_logger(), 
                "Reflex stats: samples=%lu, latency_median=%lu ns, latency_p99=%lu ns",
                sample_count_, median, p99);
        }
        
        sample_count_ = 0;
    }
    
    // Channels
    std::unique_ptr<SharedChannel> force_channel_;
    std::unique_ptr<SharedChannel> command_channel_;
    std::unique_ptr<SharedChannel> telemetry_channel_;
    
    // Publishers
    rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr force_pub_;
    rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr command_pub_;
    rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr rate_pub_;
    rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr latency_median_pub_;
    rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr latency_p99_pub_;
    
    rclcpp::TimerBase::SharedPtr timer_;
    rclcpp::TimerBase::SharedPtr stats_timer_;
    
    // State
    uint64_t last_seq_ = 0;
    uint64_t sample_count_ = 0;
    std::chrono::steady_clock::time_point last_poll_time_;
    std::deque<uint64_t> latencies_;
    std::deque<uint64_t> poll_intervals_;
};

}  // namespace reflex_ros_bridge

int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<reflex_ros_bridge::TelemetryNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
