#include "../include/market_data.hpp"
#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fmt/core.h>
#include <chrono>
#include <sched.h>
#include <thread>
#include <csignal>

volatile sig_atomic_t running = 1;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        running = 0;
    }
}

void set_cpu_affinity(int cpu_id) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1) {
        perror("sched_setaffinity");
    } else {
        fmt::print("[AFFINITY] Consumer-SHM pinned to CPU {}\n", cpu_id);
    }
#elif defined(__APPLE__)
    fmt::print("[INFO] CPU affinity not supported on macOS (requested CPU {})\n", cpu_id);
#else
    fmt::print("[WARNING] CPU affinity not supported on this platform\n");
#endif
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    try {
        set_cpu_affinity(1);

        static constexpr const char* SHM_NAME = "/market_data_hft";
        static constexpr size_t SHM_SIZE = sizeof(SPSCRingBuffer);

        fmt::print("[CONSUMER-SHM] Waiting for shared memory to be created...\n");
        
        int shm_fd = -1;
        int retry_count = 0;
        const int MAX_RETRIES = 100;

        while (shm_fd == -1 && retry_count < MAX_RETRIES && running) {
            shm_fd = shm_open(SHM_NAME, O_RDWR, 0);
            if (shm_fd == -1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                retry_count++;
            }
        }

        if (shm_fd == -1) {
            fmt::print(stderr, "[ERROR] Failed to open shared memory after {} retries\n", MAX_RETRIES);
            return 1;
        }

        SPSCRingBuffer* buffer = static_cast<SPSCRingBuffer*>(
            mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)
        );

        if (buffer == MAP_FAILED) {
            perror("mmap");
            close(shm_fd);
            return 1;
        }

        fmt::print("[CONSUMER-SHM] Connected to shared memory at {:p}\n", (void*)buffer);

        uint64_t messages_received = 0;
        uint64_t total_latency_ns = 0;
        uint64_t min_latency_ns = UINT64_MAX;
        uint64_t max_latency_ns = 0;
        LatencyHistogram histogram;

        auto last_report = std::chrono::steady_clock::now();
        auto last_log_timestamp = std::chrono::system_clock::now();
        std::tm cached_tm = {};
        uint64_t cached_ns = 0;
        uint32_t last_sequence = 0;
        bool first_message = true;
        uint64_t sequence_gaps = 0;
        
        const auto timestamp_update_interval = std::chrono::milliseconds(100);

        fmt::print("[CONSUMER-SHM] Starting to consume messages...\n\n");

        while (running) {
            MarketData data;
            
            if (buffer->try_pop(data)) {
                uint64_t recv_time_ns = get_time_ns();
                uint64_t latency_ns = recv_time_ns - data.timestamp_ns;

                total_latency_ns += latency_ns;
                min_latency_ns = std::min(min_latency_ns, latency_ns);
                max_latency_ns = std::max(max_latency_ns, latency_ns);
                messages_received++;
                histogram.record(latency_ns);

                auto now_sys = std::chrono::system_clock::now();
                if (now_sys - last_log_timestamp >= timestamp_update_interval) {
                    auto time_t_now = std::chrono::system_clock::to_time_t(now_sys);
                    cached_tm = *std::localtime(&time_t_now);
                    cached_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        now_sys.time_since_epoch()).count() % 1'000'000'000;
                    last_log_timestamp = now_sys;
                }

                if (messages_received <= 10 || data.sequence % 10000 == 0) {
                    fmt::print("[{:02d}:{:02d}:{:02d}.{:09d}] {} BID={:.2f} ASK={:.2f} SEQ={} LATENCY={}ns\n",
                              cached_tm.tm_hour, cached_tm.tm_min, cached_tm.tm_sec, static_cast<int>(cached_ns),
                              data.instrument, data.bid, data.ask,
                              data.sequence, latency_ns);
                }

                if (!first_message && data.sequence != last_sequence + 1) {
                    sequence_gaps++;
                    fmt::print("[WARNING] Sequence gap: expected {}, got {} (gap: {})\n",
                              last_sequence + 1, data.sequence, data.sequence - last_sequence - 1);
                }
                last_sequence = data.sequence;
                first_message = false;

                auto now = std::chrono::steady_clock::now();
                if (now - last_report >= std::chrono::seconds(5)) {
                    if (messages_received > 0) {
                        uint64_t p50 = histogram.get_percentile(50.0);
                        uint64_t p90 = histogram.get_percentile(90.0);
                        uint64_t p99 = histogram.get_percentile(99.0);
                        uint64_t p999 = histogram.get_percentile(99.9);

                        fmt::print("\n[STATS-SHM]\n");
                        fmt::print("  Messages: {}\n", messages_received);
                        fmt::print("  Sequence Gaps: {}\n", sequence_gaps);
                        fmt::print("  Avg Latency: {:.1f}ns\n", 
                                  static_cast<double>(total_latency_ns) / messages_received);
                        fmt::print("  Min Latency: {}ns\n", min_latency_ns);
                        fmt::print("  P50 Latency: {}ns\n", p50);
                        fmt::print("  P90 Latency: {}ns\n", p90);
                        fmt::print("  P99 Latency: {}ns\n", p99);
                        fmt::print("  P99.9 Latency: {}ns\n", p999);
                        fmt::print("  Max Latency: {}ns\n", max_latency_ns);
                        fmt::print("  Rate: {:.0f} msg/s\n\n",
                                  static_cast<double>(messages_received) / 
                                  std::chrono::duration<double>(now - last_report).count());
                    }

                    last_report = now;
                    messages_received = 0;
                    total_latency_ns = 0;
                    min_latency_ns = UINT64_MAX;
                    max_latency_ns = 0;
                    sequence_gaps = 0;
                    histogram.reset();
                }
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        }

        fmt::print("\n[CONSUMER-SHM] Shutting down gracefully...\n");

        munmap(buffer, SHM_SIZE);
        close(shm_fd);

    } catch (const std::exception& e) {
        fmt::print(stderr, "[ERROR] Consumer-SHM failed: {}\n", e.what());
        return 1;
    }

    return 0;
}
