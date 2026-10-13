// Drift-free periodic scheduling, the arithmetic behind vTaskDelayUntil: the next deadline is
// previous deadline + period, not "now + period", so jitter in one cycle does not accumulate.
// When the work overran, the most recent deadline fires immediately (like vTaskDelayUntil)
// and any deadline that went by entirely is counted as missed and skipped, never replayed.
#pragma once
#include <stdint.h>

struct Periodic {
  Periodic(uint32_t period_ms, uint32_t now_ms) : period(period_ms), next(now_ms + period_ms) {}

  // Call when the work for the current cycle is finished. Returns how long to sleep (ms);
  // 0 means the deadline already passed, run the next cycle now. After the call `next` is the
  // deadline that follows the coming wake-up. Handles uint32 wrap-around (49.7 days of millis()).
  uint32_t schedule(uint32_t now_ms);

  uint32_t period;
  uint32_t next;
  uint32_t missed = 0;    // deadlines skipped because the work overran
  uint32_t overruns = 0;  // cycles that finished late (one overrun may skip several deadlines)
};
