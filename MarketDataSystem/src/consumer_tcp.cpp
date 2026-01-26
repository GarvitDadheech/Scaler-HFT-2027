#include "../include/market_data.hpp"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <fmt/core.h>
#include <chrono>
#include <sched.h>
#include <thread>
#include <csignal>

// Global flag for graceful shutdown
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
        fmt::print("[AFFINITY] Consumer-TCP pinned to CPU {}\n", cpu_id);
    }
#elif defined(__APPLE__)
    fmt::print("[INFO] CPU affinity not supported on macOS (requested CPU {})\n", cpu_id);
#else
    fmt::print("[WARNING] CPU affinity not supported on this platform\n");
#endif
}

int main() {
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    try {
        set_cpu_affinity(2); // Pin to CPU 2

        static constexpr const char* HOST = "127.0.0.1";
        static constexpr uint16_t PORT = 9001;

        fmt::print("[CONSUMER-TCP] Connecting to {}:{}\n", HOST, PORT);

        // Create socket
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1) {
            perror("socket");
            return 1;
        }

        // Connect to publisher with retry
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(PORT);
        inet_pton(AF_INET, HOST, &addr.sin_addr);

        int retry_count = 0;
        const int MAX_RETRIES = 30;
        
        while (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1 && 
               retry_count < MAX_RETRIES && running) {
            if (retry_count == 0) {
                fmt::print("[CONSUMER-TCP] Waiting for publisher...\n");
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
            retry_count++;
            
            // Recreate socket for next attempt
            close(sock);
            sock = socket(AF_INET, SOCK_STREAM, 0);
        }

        if (retry_count >= MAX_RETRIES) {
            fmt::print(stderr, "[ERROR] Failed to connect after {} retries\n", MAX_RETRIES);
            close(sock);
            return 1;
        }

        fmt::print("[CONSUMER-TCP] Connected to publisher\n");

        // Disable Nagle's algorithm on receive side too
        int nodelay = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        // Disable delayed ACK (Linux specific)
#ifdef TCP_QUICKACK
        int quickack = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_QUICKACK, &quickack, sizeof(quickack));
#endif

        // Consumer loop
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
        
        // Cache timestamp update interval (update every 100ms)
        const auto timestamp_update_interval = std::chrono::milliseconds(100);
        
        // Fixed-size buffer for incomplete lines (avoid std::string allocations)
        char recv_buffer[4096];
        char line_buffer[4096];
        size_t line_buffer_pos = 0;

        fmt::print("[CONSUMER-TCP] Starting to consume messages...\n\n");

        while (running) {
            ssize_t bytes_received = recv(sock, recv_buffer, sizeof(recv_buffer) - 1, 0);
            
            if (bytes_received == -1) {
                if (errno == EINTR) {
                    continue; // Signal interruption, continue
                }
                perror("recv");
                break;
            }

            if (bytes_received == 0) {
                fmt::print("[INFO] Publisher closed connection\n");
                break;
            }

            // Process received data and handle incomplete lines
            for (ssize_t i = 0; i < bytes_received; ++i) {
                if (recv_buffer[i] == '\n') {
                    line_buffer[line_buffer_pos] = '\0';
                    if (line_buffer_pos > 0) {
                        std::string line(line_buffer, line_buffer_pos);
                        
                        // Update cached timestamp periodically
                        auto now_sys = std::chrono::system_clock::now();
                        if (now_sys - last_log_timestamp >= timestamp_update_interval) {
                            auto time_t_now = std::chrono::system_clock::to_time_t(now_sys);
                            cached_tm = *std::localtime(&time_t_now);
                            cached_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                now_sys.time_since_epoch()).count() % 1'000'000'000;
                            last_log_timestamp = now_sys;
                        }

                        // Simple JSON parsing
                        uint64_t recv_time_ns = get_time_ns();
                        
                        try {
                            size_t ts_pos = line.find("\"timestamp_ns\":");
                            size_t seq_pos = line.find("\"sequence\":");
                            size_t bid_pos = line.find("\"bid\":");
                            size_t ask_pos = line.find("\"ask\":");
                            size_t inst_pos = line.find("\"instrument\":\"");

                            if (ts_pos != std::string::npos && seq_pos != std::string::npos) {
                                uint64_t timestamp_ns = std::stoull(line.substr(ts_pos + 15));
                                uint32_t sequence = std::stoul(line.substr(seq_pos + 11));
                                
                                double bid = 0.0;
                                double ask = 0.0;
                                std::string instrument;

                                if (bid_pos != std::string::npos) {
                                    bid = std::stod(line.substr(bid_pos + 6));
                                }
                                if (ask_pos != std::string::npos) {
                                    ask = std::stod(line.substr(ask_pos + 6));
                                }
                                if (inst_pos != std::string::npos) {
                                    size_t inst_end = line.find("\"", inst_pos + 14);
                                    if (inst_end != std::string::npos) {
                                        instrument = line.substr(inst_pos + 14, inst_end - inst_pos - 14);
                                    }
                                }

                                uint64_t latency_ns = recv_time_ns - timestamp_ns;

                                total_latency_ns += latency_ns;
                                min_latency_ns = std::min(min_latency_ns, latency_ns);
                                max_latency_ns = std::max(max_latency_ns, latency_ns);
                                messages_received++;
                                histogram.record(latency_ns);

                                // Log first few and every 10000th message
                                if (messages_received <= 10 || sequence % 10000 == 0) {
                                    fmt::print("[{:02d}:{:02d}:{:02d}.{:09d}] {} BID={:.2f} ASK={:.2f} SEQ={} LATENCY={:.1f}µs\n",
                                              cached_tm.tm_hour, cached_tm.tm_min, cached_tm.tm_sec, static_cast<int>(cached_ns),
                                              instrument.c_str(), bid, ask, sequence, latency_ns / 1000.0);
                                }

                                // Check for sequence gaps
                                if (!first_message && sequence != last_sequence + 1) {
                                    sequence_gaps++;
                                    fmt::print("[WARNING] Sequence gap: expected {}, got {} (gap: {})\n",
                                              last_sequence + 1, sequence, sequence - last_sequence - 1);
                                }
                                last_sequence = sequence;
                                first_message = false;
                            }
                        } catch (const std::exception& e) {
                            fmt::print("[WARNING] Failed to parse JSON: {}\n", e.what());
                        }
                    }
                    line_buffer_pos = 0;
                } else if (line_buffer_pos < sizeof(line_buffer) - 1) {
                    line_buffer[line_buffer_pos++] = recv_buffer[i];
                } else {
                    // Buffer overflow - reset
                    line_buffer_pos = 0;
                }
            }

            // Periodic statistics
            auto now = std::chrono::steady_clock::now();
            if (now - last_report >= std::chrono::seconds(5)) {
                if (messages_received > 0) {
                    // Calculate percentiles from histogram (no sorting needed)
                    uint64_t p50 = histogram.get_percentile(50.0);
                    uint64_t p90 = histogram.get_percentile(90.0);
                    uint64_t p99 = histogram.get_percentile(99.0);
                    uint64_t p999 = histogram.get_percentile(99.9);

                    fmt::print("\n[STATS-TCP]\n");
                    fmt::print("  Messages: {}\n", messages_received);
                    fmt::print("  Sequence Gaps: {}\n", sequence_gaps);
                    fmt::print("  Avg Latency: {:.1f}µs\n", 
                              static_cast<double>(total_latency_ns) / messages_received / 1000.0);
                    fmt::print("  Min Latency: {:.1f}µs\n", 
                              static_cast<double>(min_latency_ns) / 1000.0);
                    fmt::print("  P50 Latency: {:.1f}µs\n", p50 / 1000.0);
                    fmt::print("  P90 Latency: {:.1f}µs\n", p90 / 1000.0);
                    fmt::print("  P99 Latency: {:.1f}µs\n", p99 / 1000.0);
                    fmt::print("  P99.9 Latency: {:.1f}µs\n", p999 / 1000.0);
                    fmt::print("  Max Latency: {:.1f}µs\n", 
                              static_cast<double>(max_latency_ns) / 1000.0);
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
        }

        fmt::print("\n[CONSUMER-TCP] Shutting down gracefully...\n");
        close(sock);

    } catch (const std::exception& e) {
        fmt::print(stderr, "[ERROR] Consumer-TCP failed: {}\n", e.what());
        return 1;
    }

    return 0;
}
