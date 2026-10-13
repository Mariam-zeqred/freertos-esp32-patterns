// 06 Core pinning and priority inversion.
//
// Three tasks: L (low priority) takes a lock for LOW_HOLD_MS at a time; H (high priority)
// wakes every HIGH_PERIOD_MS and takes the same lock; M (medium priority) burns CPU for
// HOG_BURST_MS bursts and never touches the lock. With all three on DEMO_CORE and a binary
// semaphore (USE_MUTEX 0) the classic inversion appears: H waits for L, L cannot run because M
// is busy, so H's lock wait stretches to M's whole burst. Two fixes, one knob each:
//   USE_MUTEX 1   a FreeRTOS mutex has priority inheritance: L is lifted to H's priority
//                 while it holds the lock, finishes in LOW_HOLD_MS, and H waits only that long.
//   HOG_CORE 0    pin M to the other core: it no longer competes with L at all.
// H's lock-wait statistics are printed every REPORT_PERIOD_MS.
//
// The queen-bee incubator pins control, safety and sensor tasks to core 1 and comms and
// display to core 0 (CORE_* in its Config.h), so Wi-Fi work never delays the control loop,
// and guards SharedState with a mutex, not a binary semaphore.
#include <Arduino.h>
#include <esp_timer.h>

#include "Config.h"
#include "Stats.h"

static SemaphoreHandle_t g_lock;
static RunningStats g_wait(INVERSION_LIMIT_US);  // H's time-to-acquire, in us
static SemaphoreHandle_t g_stats_lock;

static void burn_ms(uint32_t ms) {
  int64_t end = esp_timer_get_time() + (int64_t)ms * 1000;
  while (esp_timer_get_time() < end) {
  }
}

static void low_task(void*) {  // holds the lock while "updating a record"
  for (;;) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    burn_ms(LOW_HOLD_MS);
    xSemaphoreGive(g_lock);
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void hog_task(void*) {  // CPU-bound, never touches the lock
  for (;;) {
    burn_ms(HOG_BURST_MS);
    vTaskDelay(pdMS_TO_TICKS(HOG_REST_MS));
  }
}

static void high_task(void*) {  // the "control loop": must get the lock promptly
  TickType_t last_wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(HIGH_PERIOD_MS));
    int64_t t0 = esp_timer_get_time();
    xSemaphoreTake(g_lock, portMAX_DELAY);
    int64_t waited = esp_timer_get_time() - t0;
    xSemaphoreGive(g_lock);
    xSemaphoreTake(g_stats_lock, portMAX_DELAY);
    g_wait.add(waited);
    xSemaphoreGive(g_stats_lock);
  }
}

static void report_task(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(REPORT_PERIOD_MS));
    xSemaphoreTake(g_stats_lock, portMAX_DELAY);
    RunningStats s = g_wait;
    g_wait.reset();
    xSemaphoreGive(g_stats_lock);
    Serial.printf("[H] lock wait over %lu takes: mean=%.0f us  max=%lld us  inverted(>%d us)=%lu\n",
                  (unsigned long)s.count, s.mean(), (long long)s.max, INVERSION_LIMIT_US,
                  (unsigned long)s.over);
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
#if USE_MUTEX
  g_lock = xSemaphoreCreateMutex();  // priority inheritance built in
#else
  g_lock = xSemaphoreCreateBinary();  // no inheritance: a plain token
  xSemaphoreGive(g_lock);
#endif
  g_stats_lock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(low_task, "L", 2048, nullptr, 1, nullptr, DEMO_CORE);
  xTaskCreatePinnedToCore(hog_task, "M", 2048, nullptr, 2, nullptr, HOG_CORE);
  xTaskCreatePinnedToCore(high_task, "H", 2048, nullptr, 3, nullptr, DEMO_CORE);
  xTaskCreatePinnedToCore(report_task, "report", 3072, nullptr, 1, nullptr, 1 - HOG_CORE);
  Serial.printf("06_core_pinning: %s, L/H on core %d, hog on core %d, hog burst %d ms, L holds %d ms\n",
                USE_MUTEX ? "mutex (inheritance)" : "binary semaphore (no inheritance)", DEMO_CORE,
                HOG_CORE, HOG_BURST_MS, LOW_HOLD_MS);
}

void loop() { vTaskDelay(portMAX_DELAY); }
