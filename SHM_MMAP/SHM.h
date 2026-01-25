#pragma once
#include <atomic>
#include <cstdint>

static constexpr uint32_t CAPACITY =  4*1024 * 1024;

struct SHM {
  std::atomic<uint32_t> pushPtr{0};   // write index (producer)
  std::atomic<uint32_t> popPtr{0};    // read index (consumer)
  int arr[CAPACITY];

  bool push(int val) {
    uint32_t push = pushPtr.load(std::memory_order_relaxed);
    uint32_t pop  = popPtr.load(std::memory_order_acquire);

    uint32_t next = (push + 1) % CAPACITY;
    if (next == pop) {      // FULL
      return false;
    }

    arr[push] = val;        // write payload first
    pushPtr.store(next, std::memory_order_release);
    return true;
  }

  bool pop(int &out) {
    uint32_t pop  = popPtr.load(std::memory_order_relaxed);
    uint32_t push = pushPtr.load(std::memory_order_acquire);

    if (pop == push) {      // EMPTY
      return false;
    }

    out = arr[pop];         // read payload first
    uint32_t next = (pop + 1) % CAPACITY;
    popPtr.store(next, std::memory_order_release);
    return true;
  }

  bool empty() const {
    return popPtr.load(std::memory_order_acquire)
         == pushPtr.load(std::memory_order_acquire);
  }

  bool full() const {
    uint32_t push = pushPtr.load(std::memory_order_acquire);
    uint32_t pop  = popPtr.load(std::memory_order_acquire);
    return ((push + 1) % CAPACITY) == pop;
  }

  uint32_t size() const {
    uint32_t push = pushPtr.load(std::memory_order_acquire);
    uint32_t pop  = popPtr.load(std::memory_order_acquire);
    return (push + CAPACITY - pop) % CAPACITY;
  }
};
