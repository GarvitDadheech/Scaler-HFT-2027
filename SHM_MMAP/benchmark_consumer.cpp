#include <benchmark/benchmark.h>
#include "consumer.h"

static void BM_ConsumerRun(benchmark::State& state) {
  const std::string shm_name = "/kartik_shm";
  size_t totalBytes = 16 * 1024 * 1024;

  Consumer consumer(shm_name, totalBytes);

  for (auto _ : state) {
    consumer.run();     // measure full run()
  }
}

BENCHMARK(BM_ConsumerRun);
BENCHMARK_MAIN();
