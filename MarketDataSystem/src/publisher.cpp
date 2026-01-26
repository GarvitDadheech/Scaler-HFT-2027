#include "../include/market_data.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <fmt/core.h>
#include <sched.h>
#include <csignal>

// Global flag for graceful shutdown
volatile sig_atomic_t running = 1;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        running = 0;
    }
}

// Shared memory management
class SharedMemoryPublisher {
private:
    static constexpr const char* SHM_NAME = "/market_data_hft";
    int shm_fd_;
    SPSCRingBuffer* buffer_;
    size_t shm_size_;

public:
    SharedMemoryPublisher() : shm_fd_(-1), buffer_(nullptr), 
                              shm_size_(sizeof(SPSCRingBuffer)) {
        // Unlink any existing shared memory first
        shm_unlink(SHM_NAME);
        
        // Create or open shared memory
        shm_fd_ = shm_open(SHM_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        if (shm_fd_ == -1) {
            perror("shm_open");
            throw std::runtime_error("Failed to create shared memory");
        }

        // Set size
        if (ftruncate(shm_fd_, shm_size_) == -1) {
            perror("ftruncate");
            close(shm_fd_);
            throw std::runtime_error("Failed to truncate shared memory");
        }

        // Map shared memory
        buffer_ = static_cast<SPSCRingBuffer*>(
            mmap(nullptr, shm_size_, PROT_READ | PROT_WRITE, 
                 MAP_SHARED, shm_fd_, 0)
        );

        if (buffer_ == MAP_FAILED) {
            perror("mmap");
            close(shm_fd_);
            throw std::runtime_error("Failed to map shared memory");
        }

        // Initialize in-place
        new (buffer_) SPSCRingBuffer();
        fmt::print("[PUBLISHER] Shared memory initialized at {:p}\n", (void*)buffer_);
    }

    ~SharedMemoryPublisher() {
        if (buffer_ && buffer_ != MAP_FAILED) {
            munmap(buffer_, shm_size_);
        }
        if (shm_fd_ >= 0) {
            close(shm_fd_);
        }
        shm_unlink(SHM_NAME);
    }

    SPSCRingBuffer* get() { return buffer_; }
};

// TCP Server
class TCPPublisher {
private:
    int server_socket_;
    int client_socket_;
    static constexpr uint16_t PORT = 9001;
    static constexpr const char* HOST = "127.0.0.1";

public:
    TCPPublisher() : server_socket_(-1), client_socket_(-1) {
        // Create socket
        server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket_ == -1) {
            perror("socket");
            throw std::runtime_error("Failed to create socket");
        }

        // Allow reuse of address
        int reuse = 1;
        if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, 
                      &reuse, sizeof(reuse)) == -1) {
            perror("setsockopt SO_REUSEADDR");
        }

        // Set socket to non-blocking for accept
        int flags = fcntl(server_socket_, F_GETFL, 0);
        fcntl(server_socket_, F_SETFL, flags | O_NONBLOCK);

        // Bind
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(PORT);
        inet_pton(AF_INET, HOST, &addr.sin_addr);

        if (bind(server_socket_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            perror("bind");
            close(server_socket_);
            throw std::runtime_error("Failed to bind socket");
        }

        // Listen
        if (listen(server_socket_, 1) == -1) {
            perror("listen");
            close(server_socket_);
            throw std::runtime_error("Failed to listen");
        }

        fmt::print("[TCP PUBLISHER] Listening on {}:{}\n", HOST, PORT);
    }

    ~TCPPublisher() {
        if (client_socket_ >= 0) close(client_socket_);
        if (server_socket_ >= 0) close(server_socket_);
    }

    bool accept_client() {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);

        client_socket_ = accept(server_socket_, (struct sockaddr*)&addr, &addr_len);
        if (client_socket_ == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("accept");
            }
            return false;
        }

        // Disable Nagle's algorithm for low latency
        int nodelay = 1;
        if (setsockopt(client_socket_, IPPROTO_TCP, TCP_NODELAY, 
                      &nodelay, sizeof(nodelay)) == -1) {
            perror("setsockopt TCP_NODELAY");
        }

        // Disable delayed ACK (Linux specific)
#ifdef TCP_QUICKACK
        int quickack = 1;
        if (setsockopt(client_socket_, IPPROTO_TCP, TCP_QUICKACK, 
                      &quickack, sizeof(quickack)) == -1) {
            // Not critical if it fails (not available on all systems)
        }
#endif

        // Set client socket to non-blocking
        int flags = fcntl(client_socket_, F_GETFL, 0);
        fcntl(client_socket_, F_SETFL, flags | O_NONBLOCK);

        fmt::print("[TCP PUBLISHER] Client connected\n");
        return true;
    }

    bool send_data(const std::string& json_data) {
        if (client_socket_ < 0) return false;

        // Add newline for line-based protocol
        std::string message = json_data + "\n";

        ssize_t sent = send(client_socket_, message.c_str(), message.size(), MSG_DONTWAIT);
        if (sent == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
            fmt::print("[TCP PUBLISHER] Send error: {}\n", strerror(errno));
            close(client_socket_);
            client_socket_ = -1;
            return false;
        }

        return true;
    }

    bool has_client() const {
        return client_socket_ >= 0;
    }
};

// CPU Affinity utilities
void set_cpu_affinity(int cpu_id) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);

    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1) {
        perror("sched_setaffinity");
        fmt::print("[WARNING] Failed to set CPU affinity\n");
    } else {
        fmt::print("[AFFINITY] Pinned to CPU {}\n", cpu_id);
    }
#elif defined(__APPLE__)
    // macOS doesn't support CPU affinity in the same way as Linux
    // Thread affinity APIs exist but are different
    fmt::print("[INFO] CPU affinity not supported on macOS (requested CPU {})\n", cpu_id);
#else
    fmt::print("[WARNING] CPU affinity not supported on this platform\n");
#endif
}

// Main publisher loop
int main() {
    // Setup signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    try {
        // Pin publisher to CPU 0
        set_cpu_affinity(0);

        // Initialize shared memory
        SharedMemoryPublisher shm_pub;
        SPSCRingBuffer* buffer = shm_pub.get();

        // Initialize TCP server
        TCPPublisher tcp_pub;

        // Wait for client to connect (with timeout)
        fmt::print("[MAIN] Waiting for TCP client...\n");
        
        bool client_connected = false;
        auto start = std::chrono::steady_clock::now();
        
        while (!client_connected && running &&
               std::chrono::steady_clock::now() - start < std::chrono::seconds(10)) {
            if (tcp_pub.accept_client()) {
                client_connected = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!client_connected) {
            fmt::print("[WARNING] No TCP client connected, continuing anyway\n");
        }

        // Generate and publish market data
        uint32_t sequence = 0;
        const char* instruments[] = {"RELIANCE", "INFY", "TCS", "HDFC"};
        constexpr int NUM_INSTRUMENTS = sizeof(instruments) / sizeof(instruments[0]);

        fmt::print("[MAIN] Starting market data generation\n");
        fmt::print("[MAIN] Publishing rate: 100k quotes/second\n");

        auto last_report = std::chrono::steady_clock::now();
        uint64_t total_published = 0;
        uint64_t total_failed_shm = 0;

        // Publish at 100kHz (10µs per message)
        const auto interval = std::chrono::nanoseconds(10'000);
        auto next_publish = std::chrono::steady_clock::now() + interval;

        while (running) {
            // Wait until next publish time
            auto now = std::chrono::steady_clock::now();
            if (now < next_publish) {
                std::this_thread::sleep_for(next_publish - now);
            }

            // Prepare market data
            MarketData data;
            std::strcpy(data.instrument, instruments[sequence % NUM_INSTRUMENTS]);
            data.bid = 2850.25 + (sequence % 100) * 0.05;
            data.ask = data.bid + 0.50;
            data.timestamp_ns = get_time_ns();
            data.sequence = sequence++;

            // Publish to shared memory (non-blocking)
            if (!buffer->try_push(data)) {
                total_failed_shm++;
                // Ring buffer full - this shouldn't happen in normal operation
            }

            // Publish to TCP (non-blocking)
            if (client_connected) {
                std::string json = format_json(data);
                if (!tcp_pub.send_data(json)) {
                    client_connected = false;
                }
            }

            total_published++;

            // Try to reconnect TCP if disconnected
            if (!client_connected && sequence % 10000 == 0) {
                if (tcp_pub.accept_client()) {
                    client_connected = true;
                }
            }

            // Periodic report
            auto now_report = std::chrono::steady_clock::now();
            if (now_report - last_report >= std::chrono::seconds(5)) {
                double elapsed = std::chrono::duration<double>(now_report - last_report).count();
                fmt::print("[STATS] Published: {}, SHM Full: {}, Rate: {:.0f} msg/s, Buffer: {}/{}\n",
                          total_published, total_failed_shm,
                          static_cast<double>(total_published) / elapsed,
                          buffer->available(), buffer->capacity());
                last_report = now_report;
                total_published = 0;
                total_failed_shm = 0;
            }

            next_publish += interval;
        }

        fmt::print("\n[MAIN] Shutting down gracefully...\n");

    } catch (const std::exception& e) {
        fmt::print(stderr, "[ERROR] Publisher failed: {}\n", e.what());
        return 1;
    }

    return 0;
}
