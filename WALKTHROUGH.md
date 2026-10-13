# Walkthrough (for Mariam)

## Why it is built this way

The incubator firmware uses most of these mechanisms, but inside two dozen modules nobody
can see them in isolation. This repo pulls each one into a sketch short enough to read in
five minutes, with one knob in `Config.h` that makes it fail on purpose: you understand a
queue once you have watched it drop readings.

The second idea is the split between `src/` and `lib/patterns_core/`. A FreeRTOS primitive
cannot run on a laptop, but the decision around it can: when a startup is "degraded", how
many deadlines an overrun skipped, which task went silent. Those are plain C++ with unit
tests, so `make check` proves the reasoning without a board.

## File map

- `include/Config.h` — one block per sketch; each comment says what changing it shows.
- `src/01_queue/` — `xQueueSend` with timeout 0, `xQueueReceive` with `portMAX_DELAY`;
  `SeqGaps` (core) turns sequence numbers into lost counts.
- `src/02_mutex/` — `modify()` and `snapshot()` wrap every access; `Hysteresis` (core)
  runs on the copy, outside the lock.
- `src/03_event_group/` — four init tasks set bits; `startup_decide` (core) maps bits and
  elapsed time to Run, RunDegraded or Halt.
- `src/04_timer_vs_task/` — `xTimerCreate` for the blink, `vTaskDelayUntil` for the task;
  `Periodic` (core) does the deadline arithmetic and counts misses.
- `src/05_task_watchdog/` — `esp_task_wdt_add/reset` for the real watchdog, `SoftWatchdog`
  (core) as early warning, `esp_reset_reason()` at boot.
- `src/06_core_pinning/` — `xTaskCreatePinnedToCore`, binary semaphore versus mutex,
  `RunningStats` (core) for the lock-wait figures.
- `test/test_native/test_core.cpp` — 19 test functions over the core.
- `platformio.ini` — six envs differing only in `build_src_filter`, plus `native`.

## Three questions a professor may ask

**1. Why does the producer drop readings instead of waiting for space?** Because the
sampler's job is the sampling rate. Blocking on a full queue would let a slow SD write
stretch the period and corrupt every statistic computed from the data. Dropping with a
counter keeps the rate and makes the loss measurable, so the queue length is chosen from
numbers. The incubator's 60-slot ring buffer follows the same reasoning: one minute of 1 Hz
readings between drains.

**2. What is priority inversion and how does a mutex fix it?** A high-priority task waits
for a lock held by a low-priority task; a medium-priority task that needs no lock pre-empts
the low one, so the high task effectively waits on the medium one. A FreeRTOS mutex has
priority inheritance: the holder runs at the waiter's priority until it releases, so the
wait is bounded by the critical section. A binary semaphore has no inheritance, which is
what sketch 06 measures. Pinning the CPU-bound task to the other core is the structural
fix, and why the incubator keeps control on core 1.

**3. Why have a software watchdog model next to the real one?** The hardware watchdog is the
safety net: it reboots the chip whatever the firmware does, but tells you little afterwards.
The model with a shorter timeout costs a few bytes and puts the task name and its silent
time in the log before the reset, which is what you need to fix the bug. It also makes the
policy testable on the host.

## One thing to extend

Add `uxTaskGetStackHighWaterMark` to each report line and tune the stack sizes in
`Config.h` from the numbers. Then a seventh sketch on task notifications, which replace a
one-item queue or a binary semaphore at a fraction of the cost.

## Verified and not verified

Run here: `make check` (1105 assertions) and a `g++ -fsyntax-only` pass of the six sketches
against stub headers. CI runs the tests through PlatformIO and compiles all six envs. Not
done yet: flashing a board, so every serial line in the README is an expectation, not a
capture.
