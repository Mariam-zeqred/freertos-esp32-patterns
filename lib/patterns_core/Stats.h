// Small accumulators the sketches print from. No FreeRTOS, no Arduino, no heap.
#pragma once
#include <stdint.h>

// Count / min / max / mean of integer samples (latencies in us, periods in ms, ...).
// `over_threshold` counts how many samples exceeded a limit, e.g. "wake-ups later than 5 ms".
struct RunningStats {
  explicit RunningStats(int64_t over_threshold = INT64_MAX) : threshold(over_threshold) { reset(); }
  void reset();
  void add(int64_t v);
  double mean() const { return count ? sum / count : 0.0; }

  int64_t threshold;
  uint32_t count;
  uint32_t over;  // samples > threshold
  int64_t min;
  int64_t max;
  double sum;
};

// Detects lost items in a numbered stream (seq 0,1,2,...): a consumer feeds every seq it
// receives and learns how many items never arrived and how many separate gaps there were.
struct SeqGaps {
  void seen(uint32_t seq);

  uint32_t received = 0;
  uint32_t missing = 0;   // total items skipped
  uint32_t gaps = 0;      // number of discontinuities
  uint32_t last = 0;
  bool have_last = false;
};
