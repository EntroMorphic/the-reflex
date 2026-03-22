#include "reflex_ros_bridge/channel.hpp"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>

namespace reflex_ros_bridge {

SharedChannel::SharedChannel(const std::string& name, bool create)
    : name_(name), owner_(create) {
    
    std::string shm_name = "/" + name;
    
    if (create) {
        // Remove existing if present
        shm_unlink(shm_name.c_str());
        
        // Set umask to ensure world-writable
        mode_t old_umask = umask(0);
        fd_ = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
        umask(old_umask);
        
        if (fd_ < 0) {
            throw std::runtime_error("Failed to create shared memory: " + name);
        }
        
        // Explicitly set permissions
        fchmod(fd_, 0666);
        
        if (ftruncate(fd_, sizeof(ReflexChannel)) < 0) {
            close(fd_);
            shm_unlink(shm_name.c_str());
            throw std::runtime_error("Failed to size shared memory: " + name);
        }
    } else {
        // Try to open existing, retry a few times
        for (int i = 0; i < 10; ++i) {
            fd_ = shm_open(shm_name.c_str(), O_RDWR, 0666);
            if (fd_ >= 0) break;
            usleep(100000);  // 100ms
        }
        
        if (fd_ < 0) {
            throw std::runtime_error("Failed to open shared memory: " + name);
        }
    }
    
    void* ptr = mmap(nullptr, sizeof(ReflexChannel),
                     PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    
    if (ptr == MAP_FAILED) {
        close(fd_);
        if (owner_) shm_unlink(shm_name.c_str());
        throw std::runtime_error("Failed to map shared memory: " + name);
    }
    
    channel_ = static_cast<ReflexChannel*>(ptr);
    
    if (create) {
        // Initialize channel
        channel_->sequence.store(0, std::memory_order_release);
        channel_->timestamp.store(0, std::memory_order_release);
        channel_->value.store(0, std::memory_order_release);
    }
}

SharedChannel::~SharedChannel() {
    if (channel_) {
        munmap(channel_, sizeof(ReflexChannel));
    }
    if (fd_ >= 0) {
        close(fd_);
    }
    if (owner_) {
        std::string shm_name = "/" + name_;
        shm_unlink(shm_name.c_str());
    }
}

SharedChannel::SharedChannel(SharedChannel&& other) noexcept
    : channel_(other.channel_)
    , fd_(other.fd_)
    , name_(std::move(other.name_))
    , owner_(other.owner_) {
    other.channel_ = nullptr;
    other.fd_ = -1;
    other.owner_ = false;
}

SharedChannel& SharedChannel::operator=(SharedChannel&& other) noexcept {
    if (this != &other) {
        if (channel_) munmap(channel_, sizeof(ReflexChannel));
        if (fd_ >= 0) close(fd_);
        if (owner_) shm_unlink(("/" + name_).c_str());
        
        channel_ = other.channel_;
        fd_ = other.fd_;
        name_ = std::move(other.name_);
        owner_ = other.owner_;
        
        other.channel_ = nullptr;
        other.fd_ = -1;
        other.owner_ = false;
    }
    return *this;
}

void SharedChannel::signal(uint64_t value) {
    if (!channel_) return;
    
    // Get timestamp (use CLOCK_MONOTONIC_RAW for consistency)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    uint64_t timestamp = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    
    channel_->value.store(value, std::memory_order_relaxed);
    channel_->timestamp.store(timestamp, std::memory_order_relaxed);
    channel_->sequence.fetch_add(1, std::memory_order_release);
    
    // Memory barrier (ARM)
    __asm__ volatile("dmb sy" ::: "memory");
}

uint64_t SharedChannel::wait(uint64_t last_seq) {
    if (!channel_) return 0;
    
    uint64_t seq;
    while ((seq = channel_->sequence.load(std::memory_order_acquire)) == last_seq) {
        __asm__ volatile("" ::: "memory");  // Compiler barrier
    }
    return seq;
}

uint64_t SharedChannel::try_wait(uint64_t last_seq) {
    if (!channel_) return last_seq;
    return channel_->sequence.load(std::memory_order_acquire);
}

uint64_t SharedChannel::read() const {
    if (!channel_) return 0;
    return channel_->value.load(std::memory_order_acquire);
}

uint64_t SharedChannel::sequence() const {
    if (!channel_) return 0;
    return channel_->sequence.load(std::memory_order_acquire);
}

uint64_t SharedChannel::timestamp() const {
    if (!channel_) return 0;
    return channel_->timestamp.load(std::memory_order_acquire);
}

}  // namespace reflex_ros_bridge
