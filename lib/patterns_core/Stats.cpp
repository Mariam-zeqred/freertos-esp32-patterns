#include "Stats.h"

void RunningStats::reset() {
  count = 0;
  over = 0;
  min = INT64_MAX;
  max = INT64_MIN;
  sum = 0.0;
}

void RunningStats::add(int64_t v) {
  count++;
  sum += static_cast<double>(v);
  if (v < min) min = v;
  if (v > max) max = v;
  if (v > threshold) over++;
}

void SeqGaps::seen(uint32_t seq) {
  received++;
  if (have_last) {
    uint32_t expected = last + 1;
    if (seq != expected) {
      gaps++;
      if (seq > expected) missing += seq - expected;  // seq < expected: restart, not a gap
    }
  }
  last = seq;
  have_last = true;
}
