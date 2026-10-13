// Startup sequencing: which subsystems are ready (one bit each, the same bits the event
// group uses) and what to do when the deadline passes with some still missing.
#pragma once
#include <stddef.h>
#include <stdint.h>

enum StartupBit : uint32_t {
  BIT_SENSOR = 1u << 0,   // the SHT31 answered a self-test
  BIT_STORAGE = 1u << 1,  // SD card / NVS mounted
  BIT_WIFI = 1u << 2,     // got an IP
  BIT_TIME = 1u << 3,     // NTP set the clock (needs Wi-Fi first)
};
constexpr uint32_t BITS_ALL = BIT_SENSOR | BIT_STORAGE | BIT_WIFI | BIT_TIME;
constexpr uint32_t BITS_CLOUD = BIT_WIFI | BIT_TIME;  // what the uploader needs

enum class StartupDecision {
  Wait,         // deadline not reached, something still missing
  Run,          // everything ready
  RunDegraded,  // deadline passed; sensor is up, so control locally without the rest
  Halt,         // deadline passed and no sensor: nothing safe to do but wait and retry
};

StartupDecision startup_decide(uint32_t bits, uint32_t elapsed_ms, uint32_t deadline_ms);

// Writes the names of the missing bits as "wifi,time" into buf; returns the count missing.
int startup_missing(uint32_t bits, char* buf, size_t n);
