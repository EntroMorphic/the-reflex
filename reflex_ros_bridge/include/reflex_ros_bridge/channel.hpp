#pragma once

#include <cstdint>
#include <atomic>
#include <string>

namespace reflex_ros_bridge {

// Must match reflex.h structure exactly
struct alignas(64) ReflexChannel {
    std::atomic<uint64_t> sequence;
    std::atomic<uint64_t> timestamp;
    std::atomic<uint64_t> value;
    char padding[40];
};

static_assert(sizeof(ReflexChannel) == 64, "ReflexChannel must be 64 bytes");

class SharedChannel {
public:
    SharedChannel(const std::string& name, bool create = false);
    ~SharedChannel();
    
    // Non-copyable
    SharedChannel(const SharedChannel&) = delete;
    SharedChannel& operator=(const SharedChannel&) = delete;
    
    // Move semantics
    SharedChannel(SharedChannel&& other) noexcept;
    SharedChannel& operator=(SharedChannel&& other) noexcept;
    
    // Signal: write value and increment sequence (producer)
    void signal(uint64_t value);
    
    // Wait: spin until sequence changes (consumer)
    uint64_t wait(uint64_t last_seq);
    
    // Try wait: return immediately with current sequence
    uint64_t try_wait(uint64_t last_seq);
    
    // Read: get current value (non-blocking)
    uint64_t read() const;
    
    // Get current sequence number
    uint64_t sequence() const;
    
    // Get timestamp of last signal
    uint64_t timestamp() const;
    
    // Check if channel is valid
    bool valid() const { return channel_ != nullptr; }
    
    // Get channel name
    const std::string& name() const { return name_; }

private:
    ReflexChannel* channel_ = nullptr;
    int fd_ = -1;
    std::string name_;
    bool owner_ = false;
};

// Encoding/decoding helpers
inline uint64_t encode_force(double force_newtons) {
    return static_cast<uint64_t>(force_newtons * 1000000.0);  // μN precision
}

inline double decode_force(uint64_t value) {
    return static_cast<double>(value) / 1000000.0;
}

inline uint64_t encode_position(double position) {
    return static_cast<uint64_t>(position * 1000000.0);  // μm precision
}

inline double decode_position(uint64_t value) {
    return static_cast<double>(value) / 1000000.0;
}

}  // namespace reflex_ros_bridge
