# TODO

## Open (QC 2026-10-13)

- **Hardware run (needs an ESP32 DevKit).** Nothing has been flashed. Run each sketch,
  replace the "expected shape" lines under README "What you see" with real captures, and
  for `06_core_pinning` paste the `[H]` line for `USE_MUTEX 0`, `USE_MUTEX 1` and `HOG_CORE 0`.
- **`02_mutex` torn read may never appear.** With `LOCKING 0` the two writes (`seq`,
  `seq_copy`) are adjacent 32-bit stores, so the window for a torn copy is tiny. If the
  count stays at zero on hardware, widen the window on purpose (a few microseconds of
  busy-wait between the two stores under `LOCKING 0`, or a larger payload) and say so in
  the sketch comment.
- **`04_timer_vs_task` overrun counter has no tolerance.** `Periodic::schedule()` counts any
  lateness of 1 ms or more as an overrun. With 1 ms ticks and `millis()` rounding the
  default log may show a small non-zero `overruns` even though the task is on time. Confirm
  on hardware; if it happens, add a tolerance (ignore lateness below 2 ms) or document it.
- **`05_task_watchdog` is tied to Arduino-ESP32 2.0.x.** `esp_task_wdt_init(timeout, panic)`
  became a config-struct call in core 3.x (ESP-IDF 5). The queen-bee-incubator `.ino`
  already has an `#if` for both; copy that pattern before moving `platformio.ini` to a 3.x
  platform.
- **`secrets.example.h`.** STYLE.md lists one for embedded repos; none is here because no
  sketch needs credentials or Wi-Fi (`.gitignore` already ignores `include/secrets.h`).
  Decide whether to add an empty placeholder or leave the exception documented in the README.
