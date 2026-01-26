#pragma once

#include <cstdint>
#include <atomic>
#include <array>
#include <string>
#include <string_view>
#include <cstring>
#include <time.h>

constexpr size_t CACHE_LINE_SIZE = 64;
constexpr size_t RING_BUFFER_SIZE = 65536;
constexpr size_t MAX_SUBSCRIBERS = 1;

struct alignas(64) MarketData {
    char instrument[32];
    double bid;
    double ask;
    uint64_t timestamp_ns;
    uint32_t sequence;
    uint32_t padding;
    char reserved[32];

    MarketData() : bid(0), ask(0), timestamp_ns(0), sequence(0), padding(0) {
        std::memset(instrument, 0, sizeof(instrument));
        std::memset(reserved, 0, sizeof(reserved));
    }
};

static_assert(sizeof(MarketData) == 128, "MarketData must be exactly 128 bytes");

class SPSCRingBuffer {
private:
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> write_idx_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> read_idx_{0};
    std::array<MarketData, RING_BUFFER_SIZE> buffer_;

    constexpr size_t mask() const {
        return RING_BUFFER_SIZE - 1;
    }

public:
    SPSCRingBuffer() = default;

    bool try_push(const MarketData& data) {
        uint64_t write_pos = write_idx_.load(std::memory_order_relaxed);
        uint64_t read_pos = read_idx_.load(std::memory_order_acquire);

        if ((write_pos + 1) - read_pos > RING_BUFFER_SIZE) {
            return false;
        }

        buffer_[write_pos & mask()] = data;
        write_idx_.store(write_pos + 1, std::memory_order_release);
        return true;
    }

    bool try_pop(MarketData& data) {
        uint64_t read_pos = read_idx_.load(std::memory_order_relaxed);
        uint64_t write_pos = write_idx_.load(std::memory_order_acquire);

        if (read_pos >= write_pos) {
            return false;
        }

        data = buffer_[read_pos & mask()];
        read_idx_.store(read_pos + 1, std::memory_order_relaxed);
        return true;
    }

    size_t available() const {
        uint64_t write = write_idx_.load(std::memory_order_acquire);
        uint64_t read = read_idx_.load(std::memory_order_relaxed);
        return write - read;
    }

    size_t capacity() const {
        return RING_BUFFER_SIZE;
    }
};

inline uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec * 1'000'000'000ULL + ts.tv_nsec;
}

inline std::string format_json(const MarketData& data) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
        R"({"instrument":"%s","bid":%.2f,"ask":%.2f,"timestamp_ns":%llu,"sequence":%u})",
        data.instrument, data.bid, data.ask, 
        (unsigned long long)data.timestamp_ns, data.sequence);
    return std::string(buffer);
}

class LatencyHistogram {
private:
    static constexpr size_t BUCKET_COUNT = 1000;
    static constexpr uint64_t MAX_LATENCY_NS = 10'000'000;
    static constexpr uint64_t BUCKET_SIZE_NS = MAX_LATENCY_NS / BUCKET_COUNT;
    
    uint64_t buckets_[BUCKET_COUNT];
    uint64_t total_count_;
    
    size_t get_bucket(uint64_t latency_ns) const {
        if (latency_ns >= MAX_LATENCY_NS) {
            return BUCKET_COUNT - 1;
        }
        return static_cast<size_t>(latency_ns / BUCKET_SIZE_NS);
    }
    
public:
    LatencyHistogram() : total_count_(0) {
        for (size_t i = 0; i < BUCKET_COUNT; ++i) {
            buckets_[i] = 0;
        }
    }
    
    void record(uint64_t latency_ns) {
        size_t bucket = get_bucket(latency_ns);
        buckets_[bucket]++;
        total_count_++;
    }
    
    uint64_t get_percentile(double percentile) const {
        if (total_count_ == 0) return 0;
        
        uint64_t target_count = static_cast<uint64_t>(total_count_ * percentile / 100.0);
        uint64_t cumulative = 0;
        
        for (size_t i = 0; i < BUCKET_COUNT; ++i) {
            cumulative += buckets_[i];
            if (cumulative >= target_count) {
                return (i + 0.5) * BUCKET_SIZE_NS;
            }
        }
        return MAX_LATENCY_NS;
    }
    
    void reset() {
        for (size_t i = 0; i < BUCKET_COUNT; ++i) {
            buckets_[i] = 0;
        }
        total_count_ = 0;
    }
    
    uint64_t get_count() const { return total_count_; }
};
