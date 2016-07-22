#include <benchmark/benchmark.h>

extern "C" {

#include <paralull.h>
#include <liblfds.h>

}

/* Libflds */

static struct lfds611_queue_state *sq;

static void libflds_enqueue(benchmark::State& state) {
  if (state.thread_index == 0) {
    lfds611_queue_new(&sq, 10000);
  }
  while (state.KeepRunning()) {
    lfds611_queue_guaranteed_enqueue(sq, NULL);
  }
  if (state.thread_index == 0) {
    lfds611_queue_delete(sq, NULL, NULL);
  }
}
BENCHMARK(libflds_enqueue)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->Threads(16)
    ->Threads(32)
    ->Threads(64)
    ->Threads(128);

static void libflds_mixed(benchmark::State& state) {
  if (state.thread_index == 0) {
    lfds611_queue_new(&sq, 1000000);
  }
  while (state.KeepRunning()) {
    lfds611_queue_guaranteed_enqueue(sq, NULL);
    void *dat;
    lfds611_queue_dequeue(sq, &dat);
  }
  if (state.thread_index == 0) {
    lfds611_queue_delete(sq, NULL, NULL);
  }
}
BENCHMARK(libflds_mixed)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->Threads(16)
    ->Threads(32)
    ->Threads(64)
    ->Threads(128);

/* Paralull */

static pll_queue q;

static void paralull_enqueue(benchmark::State& state) {
  if (state.thread_index == 0) {
    q = pll_queue_init();
  }
  while (state.KeepRunning()) {
    pll_enqueue(q, NULL);
  }
  if (state.thread_index == 0) {
    pll_queue_term(q);
  }
}
BENCHMARK(paralull_enqueue)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->Threads(16)
    ->Threads(32)
    ->Threads(64)
    ->Threads(128);

static void paralull_mixed(benchmark::State& state) {
  if (state.thread_index == 0) {
    q = pll_queue_init();
  }
  while (state.KeepRunning()) {
    pll_enqueue(q, NULL);
    pll_dequeue(q);
  }
  if (state.thread_index == 0) {
    pll_queue_term(q);
  }
}
BENCHMARK(paralull_mixed)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->Threads(16)
    ->Threads(32)
    ->Threads(64)
    ->Threads(128);

BENCHMARK_MAIN()
