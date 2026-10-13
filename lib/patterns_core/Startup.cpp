#include "Startup.h"

#include <stdio.h>
#include <string.h>

StartupDecision startup_decide(uint32_t bits, uint32_t elapsed_ms, uint32_t deadline_ms) {
  if ((bits & BITS_ALL) == BITS_ALL) return StartupDecision::Run;
  if (elapsed_ms < deadline_ms) return StartupDecision::Wait;
  if (!(bits & BIT_SENSOR)) return StartupDecision::Halt;
  return StartupDecision::RunDegraded;
}

int startup_missing(uint32_t bits, char* buf, size_t n) {
  static const struct { uint32_t bit; const char* name; } table[] = {
      {BIT_SENSOR, "sensor"}, {BIT_STORAGE, "storage"}, {BIT_WIFI, "wifi"}, {BIT_TIME, "time"}};
  int count = 0;
  size_t used = 0;
  if (n) buf[0] = '\0';
  for (const auto& t : table) {
    if (bits & t.bit) continue;
    int w = snprintf(buf + used, n > used ? n - used : 0, "%s%s", count ? "," : "", t.name);
    if (w > 0) used += static_cast<size_t>(w);
    count++;
  }
  return count;
}
