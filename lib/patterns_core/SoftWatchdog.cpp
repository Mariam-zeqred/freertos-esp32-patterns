#include "SoftWatchdog.h"

int SoftWatchdog::add(const char* name, uint32_t now_ms) {
  for (int i = 0; i < kMaxTasks; i++) {
    if (!entries[i].used) {  // field by field: the Arduino toolchain compiles as C++11
      entries[i].name = name;
      entries[i].last_kick = now_ms;
      entries[i].used = true;
      return i;
    }
  }
  return -1;
}

bool SoftWatchdog::remove(int slot) {
  if (slot < 0 || slot >= kMaxTasks || !entries[slot].used) return false;
  entries[slot] = Entry{};
  return true;
}

bool SoftWatchdog::kick(int slot, uint32_t now_ms) {
  if (slot < 0 || slot >= kMaxTasks || !entries[slot].used) return false;
  entries[slot].last_kick = now_ms;
  return true;
}

const char* SoftWatchdog::check(uint32_t now_ms, uint32_t* silent_ms) const {
  for (const Entry& e : entries) {
    if (!e.used) continue;
    uint32_t silent = now_ms - e.last_kick;  // wrap-safe for spans under 49 days
    if (silent > timeout) {
      if (silent_ms) *silent_ms = silent;
      return e.name;
    }
  }
  return nullptr;
}

int SoftWatchdog::subscribed() const {
  int n = 0;
  for (const Entry& e : entries) n += e.used ? 1 : 0;
  return n;
}
