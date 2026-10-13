# freertos-esp32-patterns

Six minimal, commented ESP32 sketches, one FreeRTOS pattern each: a producer/consumer queue,
mutex-guarded shared state, an event group for startup sequencing, software timers against
tasks, the task watchdog catching a stalled task, and core pinning with a priority-inversion
demo. Each sketch is a PlatformIO environment, each prints statistics to the serial monitor
that show the pattern working (or, with one knob in `include/Config.h`, failing). The logic
that does not need FreeRTOS lives in `lib/patterns_core/` and is unit-tested on the host.

```
 include/Config.h ──── one set of knobs for all sketches
        │
 src/0N_<pattern>/main.cpp ── tasks, queues, semaphores, event groups, timers (needs the board)
        │ calls
 lib/patterns_core/ ─────── Stats, Hysteresis, Startup, Periodic, SoftWatchdog (plain C++)
        ▲ tested by
 test/test_native/test_core.cpp ── `make check` (g++) or `pio test -e native`
```

## The six patterns

| Env | Pattern | Use it when | Knob to break it | In [queen-bee-incubator](https://github.com/Mariam-zeqred/queen-bee-incubator) |
|---|---|---|---|---|
| `01_queue` | producer/consumer queue | one task samples at a fixed rate, another consumes in bursts (SD, cloud) | `Q_LENGTH 8` loses readings during a flush; 16 does not | `RingBuffer` (60 slots) filled by Control, drained once a minute by Datalog |
| `02_mutex` | mutex-guarded shared state | several tasks read and write one struct | `LOCKING 0` counts torn reads | `SharedState` module, `MUTEX_LOCK_TIMEOUT_MS 100`; `Interlock` is the single writer of the heater and fan outputs |
| `03_event_group` | event group for startup | bring-up steps run in parallel, some may fail, workers must not start early | `STORAGE_PRESENT 0` → run degraded; `SENSOR_PRESENT 0` → halt | not used: its `setup()` brings modules up one after another, fan relay first |
| `04_timer_vs_task` | software timer vs task | short periodic work (timer) vs work that may block (task) | `TIMER_B_BLOCK_MS 300` makes every timer jitter; `TASK_OVERRUN_MS 1200` shows `missed` without drift | tasks only, at fixed rates (1 Hz control, 10 Hz safety, 5 Hz display) |
| `05_task_watchdog` | task watchdog + stalled task | a task that silently stops must reboot the device | `STALL_MODE` 0 spins, 1 blocks forever | Control and Safety subscribe, `TASK_WDT_TIMEOUT_S 30` |
| `06_core_pinning` | core pinning, priority inversion | a control task shares a lock with a low-priority task while something CPU-bound runs | `USE_MUTEX 1` (inheritance) or `HOG_CORE 0` (other core) each fix it | control, safety, sensor on core 1; comms and display on core 0 (`CORE_*` in its `Config.h`) |

## Run it

Needs PlatformIO (`pip install platformio`) and an ESP32 DevKit. No Wi-Fi, no secrets, no
sensors: every input is simulated so each sketch runs on a bare board.

```bash
make check                                   # host tests of lib/patterns_core (g++)
pio run -e 01_queue -t upload && pio device monitor
# then edit include/Config.h, e.g. Q_LENGTH 16, and flash again:
pio run -e 01_queue -t upload && pio device monitor
```

`make build` compiles all six environments. Each env compiles only its own `src/` folder
(`build_src_filter` in `platformio.ini`), so the sketches share `Config.h` and the core but not
each other's code.

## What you see

The host tests (`make check`), run in this repo:

```
  periodic_does_not_drift
  periodic_counts_missed_deadlines
  ...
  watchdog_is_bounded
1105 checks passed, 0 failed
```

What each sketch prints every 5 s is written in its header comment. The lines below are
the **expected shape** of the output, not captured logs: the sketches compile in CI but have
not been run on a board yet (see Limitations).

```
[consumer] received=200 lost=35 (in 5 gaps)  producer dropped=35  queue depth max=8 mean=3.1 of 8
[display] temp=34.91 set=35.0 heater=ON  reads=25 torn=0 lock_timeouts=0
[startup]  5000 ms  bits=0x0d  missing: storage
[startup] deadline passed without storage: control only, no uploads
[timer A] n=10 late>2000us: 0  max=312 us  | [task C] n=10 late: 0  max=180 us  overruns=0 missed=0
[monitor]  17500 ms: task 'comms' silent for 2480 ms
[H] lock wait over 50 takes: mean=38000 us  max=201000 us  inverted(>30000 us)=12
```

## Design notes

- **A sampler never blocks on its consumer.** `01_queue` sends with timeout 0 and counts
  drops; the consumer sees the holes through `SeqGaps`. Losing a reading visibly beats
  stalling the sampling rate invisibly.
- **Copy out, then compute.** `02_mutex` takes the lock only to copy the struct in or out;
  the hysteresis decision runs on the copy. Short critical sections make lock timeouts rare,
  and a timeout is counted rather than waited on forever.
- **Decide once, at startup.** `03_event_group` waits for the bits with a deadline and then
  calls `startup_decide` (host-tested), so the policy "no sensor → halt, no cloud → run
  locally" is a pure function you can read in one screen, not scattered `if (wifi_ok)` checks.
- **Timers share one task.** All software timers run their callbacks in the timer service
  task. `04_timer_vs_task` shows that one blocking callback delays all the others, while a
  task with `vTaskDelayUntil` keeps its phase; `Periodic` turns an overrun into a `missed`
  count instead of drift.
- **The watchdog needs a name before the reset.** The TWDT panic lists the task, but by then
  the device is rebooting. `05_task_watchdog` runs `SoftWatchdog` with a shorter timeout so
  the log says who went silent, and prints the reset reason at the next boot.
- **Priority inheritance is why you use a mutex, not a binary semaphore.** `06_core_pinning`
  measures the high-priority task's lock wait; with a binary semaphore it grows to the CPU
  hog's whole burst, with a mutex it is bounded by the holder's critical section. Moving
  the hog to the other core is the other fix, and the reason the incubator keeps its
  control tasks on core 1.

## Tests and checks

| Command | What it proves | Run here |
|---|---|---|
| `make check` | 1105 assertions on `lib/patterns_core` (stats and gap detection, hysteresis edges and bad readings, startup decisions and the missing-bit string, drift-free periods including `millis()` wrap-around, the software watchdog) | yes, passing |
| `pio test -e native` | the same tests through PlatformIO's native env and the custom runner in `test/` | not in the build sandbox (registry unreachable); `host-checks` job in CI |
| `pio run -e <env>` ×6 | each sketch compiles (espressif32 6.x, Arduino core 2.0.x) | not in the build sandbox; `firmware-build` matrix in CI |
| host syntax check | all six sketches pass `g++ -fsyntax-only -Wall -Wextra -Werror` against stub FreeRTOS headers | yes, passing (stubs not in the repo) |

## Layout

```
platformio.ini            six esp32dev envs (one per sketch) + native
include/Config.h          every knob, grouped per sketch
src/01_queue … 06_core_pinning/main.cpp   one pattern per folder, ~70–120 lines each
lib/patterns_core/        Stats, Hysteresis, Startup, Periodic, SoftWatchdog (no Arduino)
test/test_native/         host unit tests; test/minitest.h; test/test_custom_runner.py for pio
Makefile                  make check / build / clean
.github/workflows/ci.yml  host tests + six firmware compiles
```

## Limitations

- Compiled in CI, not yet run on hardware. The numbers under "What you see" are the expected
  shape of the output, not measurements; replace them with real serial captures after a run.
- The torn-read count in `02_mutex` with `LOCKING 0` depends on timing and may stay at zero
  for a while; the two writes are only a few instructions apart.
- `05_task_watchdog` relies on Arduino-ESP32 2.0.x (`esp_task_wdt_init(timeout, panic)`); the
  3.x core (ESP-IDF 5) changed that API to a config struct.
- Every input is simulated. The sketches show the mechanisms, not a sensor driver.

## Next steps

Run each sketch on a DevKit and paste the real logs into this README; add a seventh sketch
for task notifications (`xTaskNotify`) as the lightweight alternative to a one-item queue;
and a stack high-water-mark report (`uxTaskGetStackHighWaterMark`) to size the stacks in
`Config.h` from evidence instead of guesses.

MIT licence.
