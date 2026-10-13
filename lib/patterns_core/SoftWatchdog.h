// A model of what the ESP32 task watchdog (TWDT) does, written so it can run and be tested
// on the host: tasks subscribe, kick regularly, and `check` names the first one that has
// been silent for longer than the timeout. Sketch 05 runs it next to the real TWDT so the
// serial log explains the reset before it happens.
#pragma once
#include <stdint.h>

struct SoftWatchdog {
  static constexpr int kMaxTasks = 8;

  explicit SoftWatchdog(uint32_t timeout_ms) : timeout(timeout_ms) {}

  int add(const char* name, uint32_t now_ms);  // returns a slot, or -1 if full
  bool remove(int slot);
  bool kick(int slot, uint32_t now_ms);
  // First subscribed task that has not kicked within `timeout`, or nullptr. Optionally
  // reports how long it has been silent.
  const char* check(uint32_t now_ms, uint32_t* silent_ms = nullptr) const;
  int subscribed() const;

  struct Entry {
    const char* name = nullptr;
    uint32_t last_kick = 0;
    bool used = false;
  };
  Entry entries[kMaxTasks];
  uint32_t timeout;
};
