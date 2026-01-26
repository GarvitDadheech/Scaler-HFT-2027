# Market Data System

Low-latency market data distribution system with shared memory and TCP transports.

## Architecture

```
┌─────────────────────────────────────┐
│      Publisher (Process A)          │
│  - Generates market data            │
│  - Publishes to both transports     │
└──────────┬──────────────┬───────────┘
           │              │
    ┌──────▼──────┐  ┌────▼──────────┐
    │ Shared Mem  │  │  TCP Socket   │
    │ (Lock-Free) │  │ (127.0.0.1)   │
    └──────┬──────┘  └────┬──────────┘
           │              │
    ┌──────▼──────┐  ┌────▼──────────┐
    │Consumer-SHM │  │Consumer-TCP   │
    │  (Process B)│  │  (Process C)  │
    └─────────────┘  └───────────────┘
```

**Components:**
- **Publisher**: Generates market data, publishes via shared memory (SPSC ring buffer) and TCP
- **Consumer-SHM**: Reads from lock-free shared memory ring buffer
- **Consumer-TCP**: Reads from TCP loopback socket

## Implementation

### SPSC Ring Buffer

Lock-free single-producer, single-consumer ring buffer using `std::atomic`:

```cpp
class SPSCRingBuffer {
    alignas(64) std::atomic<uint64_t> write_idx_;  // Cache line 0
    alignas(64) std::atomic<uint64_t> read_idx_;   // Cache line 1
    std::array<MarketData, 65536> buffer_;         // Power-of-2 size
};
```

- **Size**: 65536 entries (power-of-2 for fast modulo with bitwise AND)
- **Memory ordering**: `acquire/release` semantics for synchronization
- **Cache-line padding**: 64-byte alignment prevents false sharing
- **Operations**: `try_push()` (non-blocking), `try_pop()` (non-blocking)

### Shared Memory Transport

- Uses `shm_open()` and `mmap()` for zero-copy data transfer
- Shared memory name: `/market_data_hft`
- Publisher creates and initializes, consumer opens read-only
- Consumer waits for publisher to create shared memory before connecting

### TCP Transport

- Loopback socket on `127.0.0.1:9001`
- Optimizations:
  - `TCP_NODELAY`: Disables Nagle's algorithm
  - `TCP_QUICKACK`: Disables delayed ACK (Linux)
  - Non-blocking I/O
- Line-based protocol: JSON messages terminated with `\n`

### Market Data Format

Fixed-size structure (128 bytes, 64-byte aligned):

```cpp
struct MarketData {
    char instrument[32];
    double bid;
    double ask;
    uint64_t timestamp_ns;  // CLOCK_MONOTONIC_RAW
    uint32_t sequence;
    // padding...
};
```

JSON representation:
```json
{"instrument":"RELIANCE","bid":2850.25,"ask":2850.75,"timestamp_ns":1234567890123,"sequence":42}
```

### Latency Measurement

- Publisher timestamps each message with `get_time_ns()` (CLOCK_MONOTONIC_RAW)
- Consumer calculates latency: `recv_time_ns - data.timestamp_ns`
- Histogram-based percentile calculation (1000 buckets, no sorting)
- Statistics: P50, P90, P99, P99.9, min, max, average

### Performance Optimizations

- **CPU Affinity**: Publisher on CPU 0, Consumer-SHM on CPU 1, Consumer-TCP on CPU 2
- **Fixed-size buffers**: No dynamic allocations in hot paths
- **Cached timestamps**: System clock updated every 100ms for logging
- **Histogram**: O(1) insertion, O(n) percentile calculation

### Build

```bash
cd MarketDataSystem
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

**Dependencies**: C++17, CMake 3.20+, `fmt` library

### Run

**Terminal 1 - Publisher:**
```bash
./publisher
```

**Terminal 2 - Consumer-SHM:**
```bash
./consumer_shm
```

**Terminal 3 - Consumer-TCP:**
```bash
./consumer_tcp
```

### Code Structure

```
MarketDataSystem/
├── CMakeLists.txt
├── include/
│   └── market_data.hpp     # SPSC Ring Buffer, MarketData, LatencyHistogram
└── src/
    ├── publisher.cpp        # Publisher with SHM + TCP
    ├── consumer_shm.cpp     # Shared memory consumer
    └── consumer_tcp.cpp     # TCP consumer
```
