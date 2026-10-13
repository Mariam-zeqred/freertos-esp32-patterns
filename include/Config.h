// Tuning knobs for all six sketches. Change a value, rebuild one env, watch the serial log.
#pragma once

// --- common ---
#define SERIAL_BAUD 115200
#define REPORT_PERIOD_MS 5000      // how often each sketch prints its statistics
#define LED_PIN 2                  // on-board LED of most ESP32 DevKit boards

// --- 01_queue: producer/consumer ---
#define Q_LENGTH 8                 // 8: the consumer's flush loses readings; 16: none lost
#define Q_PRODUCE_PERIOD_MS 100    // sampler rate (10 Hz)
#define Q_FLUSH_EVERY 20           // the consumer "writes a block" after this many items ...
#define Q_FLUSH_BLOCK_MS 1500      // ... and is busy for this long, during which the queue fills

// --- 02_mutex: shared state ---
#define LOCKING 1                  // 0: skip the mutex and count torn reads
#define LOCK_TIMEOUT_MS 100        // same default the queen-bee incubator uses (MUTEX_LOCK_TIMEOUT_MS)
#define SENSOR_PERIOD_MS 5         // fast writer so a torn read has a chance to show up
#define CONTROL_PERIOD_MS 1000
#define DISPLAY_PERIOD_MS 200
#define SETPOINT_TOGGLE_MS 7000    // the "user" flips the setpoint between the two values below
#define SETPOINT_A_C 35.0f
#define SETPOINT_B_C 34.5f
#define HYST_BAND_C 0.3f

// --- 03_event_group: startup sequencing ---
#define STARTUP_DEADLINE_MS 5000   // wait this long for every subsystem, then decide
#define SIM_SENSOR_MS 500          // simulated bring-up times
#define SIM_STORAGE_MS 800
#define SIM_WIFI_MS 1500
#define SIM_NTP_MS 1500            // after Wi-Fi
#define STORAGE_PRESENT 0          // 0: storage never comes up -> RunDegraded after the deadline
#define SENSOR_PRESENT 1           // 0: sensor never answers  -> Halt

// --- 04_timer_vs_task ---
#define BLINK_PERIOD_MS 500        // both the timer and the task try to toggle at this rate
#define TIMER_B_PERIOD_MS 1000
#define TIMER_B_BLOCK_MS 0         // set to 300: a blocking timer callback stalls *every* timer
#define TASK_OVERRUN_EVERY 10      // every Nth cycle of the task takes ...
#define TASK_OVERRUN_MS 0          // ... this long (try 1200: watch `missed` count, no drift)
#define JITTER_LIMIT_US 2000       // a wake-up later than this counts as "late"

// --- 05_task_watchdog ---
#define WDT_TIMEOUT_S 5            // hardware task watchdog (queen-bee incubator uses 30)
#define SOFT_WDT_TIMEOUT_MS 2000   // the early-warning monitor, shorter than the TWDT
#define KICK_PERIOD_MS 1000
#define STALL_AFTER_MS 15000       // the "comms" task stops kicking after this long
#define STALL_MODE 0               // 0: busy loop (hogs its core), 1: blocks forever on a semaphore

// --- 06_core_pinning: priority inversion ---
#define USE_MUTEX 0                // 0: binary semaphore (no inheritance) -> inversion visible; 1: mutex
#define DEMO_CORE 1                // the low- and high-priority tasks that share the lock
#define HOG_CORE 1                 // the medium-priority CPU hog; set 0 to move it off the demo core
#define LOW_HOLD_MS 20             // how long the low-priority task holds the lock
#define HOG_BURST_MS 200           // the hog's CPU burst
#define HOG_REST_MS 50
#define HIGH_PERIOD_MS 100
#define INVERSION_LIMIT_US 30000   // a lock wait longer than this means the hog got in between
