// 04 Software timers vs tasks.
//
// Two ways to do something every BLINK_PERIOD_MS:
//   timer A  - a FreeRTOS software timer. Cheap (no stack of its own) but its callback runs
//              in the single timer service task, so it must return quickly.
//   task C   - a task with vTaskDelayUntil: its own stack, may block, keeps phase.
// Timer B shows the classic mistake: a callback that blocks for TIMER_B_BLOCK_MS delays
// every other timer, so timer A's jitter grows while task C is unaffected.
// Task C also feeds Periodic (host-tested) so an overrun shows up as `missed` instead of as
// a slowly drifting schedule.
//
// The queen-bee incubator uses only tasks with fixed rates (1 Hz control, 10 Hz safety, 5 Hz
// display); nothing in it needs a software timer.
#include <Arduino.h>
#include <esp_timer.h>

#include "Config.h"
#include "Periodic.h"
#include "Stats.h"

static RunningStats g_timer_jitter(JITTER_LIMIT_US);  // us late, per callback
static RunningStats g_task_jitter(JITTER_LIMIT_US);
static Periodic g_task_sched(BLINK_PERIOD_MS, 0);
static int64_t g_timer_last_us = 0;
static bool g_led = false;

static void burn_ms(uint32_t ms) {  // simulated work that cannot be interrupted by a delay
  int64_t end = esp_timer_get_time() + (int64_t)ms * 1000;
  while (esp_timer_get_time() < end) {
  }
}

static void timer_a_cb(TimerHandle_t) {  // keep callbacks short: toggle, measure, return
  int64_t now = esp_timer_get_time();
  if (g_timer_last_us) g_timer_jitter.add(llabs(now - g_timer_last_us - (int64_t)BLINK_PERIOD_MS * 1000));
  g_timer_last_us = now;
  g_led = !g_led;
  digitalWrite(LED_PIN, g_led);
}

static void timer_b_cb(TimerHandle_t) {
  if (TIMER_B_BLOCK_MS) burn_ms(TIMER_B_BLOCK_MS);  // never do this in a timer callback
}

static void task_c(void*) {
  TickType_t last_wake = xTaskGetTickCount();
  uint32_t cycle = 0;
  int64_t expected_us = esp_timer_get_time();
  g_task_sched = Periodic(BLINK_PERIOD_MS, millis());
  for (;;) {
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BLINK_PERIOD_MS));  // phase-locked wake-up
    expected_us += (int64_t)BLINK_PERIOD_MS * 1000;
    g_task_jitter.add(llabs(esp_timer_get_time() - expected_us));
    if (TASK_OVERRUN_MS && ++cycle % TASK_OVERRUN_EVERY == 0) burn_ms(TASK_OVERRUN_MS);
    uint32_t overruns_before = g_task_sched.overruns;
    g_task_sched.schedule(millis());  // bookkeeping only; the delay above drives timing
    if (g_task_sched.overruns != overruns_before) {  // overran: vTaskDelayUntil returned at once, resync
      last_wake = xTaskGetTickCount();
      expected_us = esp_timer_get_time();
    }
  }
}

static void report_task(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(REPORT_PERIOD_MS));
    Serial.printf("[timer A] n=%lu late>%dus: %lu  max=%lld us  | [task C] n=%lu late: %lu  "
                  "max=%lld us  overruns=%lu missed=%lu\n",
                  (unsigned long)g_timer_jitter.count, JITTER_LIMIT_US, (unsigned long)g_timer_jitter.over,
                  (long long)g_timer_jitter.max, (unsigned long)g_task_jitter.count,
                  (unsigned long)g_task_jitter.over, (long long)g_task_jitter.max,
                  (unsigned long)g_task_sched.overruns, (unsigned long)g_task_sched.missed);
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  pinMode(LED_PIN, OUTPUT);
  TimerHandle_t a = xTimerCreate("blink", pdMS_TO_TICKS(BLINK_PERIOD_MS), pdTRUE, nullptr, timer_a_cb);
  TimerHandle_t b = xTimerCreate("blocker", pdMS_TO_TICKS(TIMER_B_PERIOD_MS), pdTRUE, nullptr, timer_b_cb);
  xTimerStart(a, 0);
  xTimerStart(b, 0);
  xTaskCreatePinnedToCore(task_c, "task_c", 2048, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(report_task, "report", 3072, nullptr, 1, nullptr, 0);
  Serial.printf("04_timer_vs_task: period %d ms, timer B blocks %d ms, task overrun %d ms every %d\n",
                BLINK_PERIOD_MS, TIMER_B_BLOCK_MS, TASK_OVERRUN_MS, TASK_OVERRUN_EVERY);
}

void loop() { vTaskDelay(portMAX_DELAY); }
