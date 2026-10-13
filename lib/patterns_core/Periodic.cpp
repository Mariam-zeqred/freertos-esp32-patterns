#include "Periodic.h"

uint32_t Periodic::schedule(uint32_t now_ms) {
  int32_t late = static_cast<int32_t>(now_ms - next);  // signed: works across the wrap
  if (late <= 0) {
    uint32_t sleep = next - now_ms;
    next += period;
    return sleep;
  }
  overruns++;
  uint32_t skipped = static_cast<uint32_t>(late) / period;  // deadlines that went by entirely
  missed += skipped;
  next += (skipped + 1) * period;  // the most recent passed deadline is served now
  return 0;
}
