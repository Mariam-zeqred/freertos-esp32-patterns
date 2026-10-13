// 05 Task watchdog with a deliberately stalled task.
//
// Two tasks subscribe to the ESP32 task watchdog (TWDT) and kick it every second. After
// STALL_AFTER_MS the "comms" task stops kicking: STALL_MODE 0 spins forever (hogs its core),
// STALL_MODE 1 blocks forever on a semaphore nobody gives. WDT_TIMEOUT_S later the TWDT
// panics, prints the stalled task's name and the chip reboots; at boot the sketch prints the
// reset reason so the log closes the loop.
// A third task runs SoftWatchdog (host-tested) with a shorter timeout as an early warning,
// so the serial log names the silent task before the hardware does.
//
// The queen-bee incubator subscribes its Control and Safety tasks with TASK_WDT_TIMEOUT_S 30.
#include <Arduino.h>
#include <esp_task_wdt.h>

#include "Config.h"
#include "SoftWatchdog.h"

static SoftWatchdog g_soft(SOFT_WDT_TIMEOUT_MS);
static SemaphoreHandle_t g_soft_lock;  // SoftWatchdog is plain C++; it needs guarding here
static int g_slot_control, g_slot_comms;
static SemaphoreHandle_t g_never_given;

static void kick(int slot) {
  esp_task_wdt_reset();  // the real one (for the calling task)
  if (xSemaphoreTake(g_soft_lock, pdMS_TO_TICKS(10)) == pdTRUE) {
    g_soft.kick(slot, millis());
    xSemaphoreGive(g_soft_lock);
  }
}

static void control_task(void*) {
  esp_task_wdt_add(nullptr);  // subscribe this task
  for (;;) {
    kick(g_slot_control);
    vTaskDelay(pdMS_TO_TICKS(KICK_PERIOD_MS));
  }
}

static void comms_task(void*) {
  esp_task_wdt_add(nullptr);
  uint32_t t0 = millis();
  for (;;) {
    kick(g_slot_comms);
    if (millis() - t0 > STALL_AFTER_MS) {
      Serial.printf("[comms] stalling now (mode %d); TWDT fires in %d s\n", STALL_MODE, WDT_TIMEOUT_S);
#if STALL_MODE == 0
      for (;;) {
      }  // e.g. a driver spin-wait on a bus that never answers
#else
      xSemaphoreTake(g_never_given, portMAX_DELAY);  // e.g. waiting on a lock a dead task held
#endif
    }
    vTaskDelay(pdMS_TO_TICKS(KICK_PERIOD_MS));
  }
}

static void monitor_task(void*) {  // not subscribed: it only watches
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    uint32_t silent = 0;
    const char* who = nullptr;
    if (xSemaphoreTake(g_soft_lock, pdMS_TO_TICKS(10)) == pdTRUE) {
      who = g_soft.check(millis(), &silent);
      xSemaphoreGive(g_soft_lock);
    }
    if (who)
      Serial.printf("[monitor] %6lu ms: task '%s' silent for %lu ms\n", (unsigned long)millis(), who,
                    (unsigned long)silent);
    else if (millis() % REPORT_PERIOD_MS < 500)
      Serial.printf("[monitor] %6lu ms: %d tasks kicking\n", (unsigned long)millis(), g_soft.subscribed());
  }
}

static const char* reset_reason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_TASK_WDT: return "TASK WATCHDOG";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    default: return "other";
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  Serial.printf("05_task_watchdog: boot, last reset reason: %s\n", reset_reason());
  // Arduino-ESP32 2.x already started the TWDT with the IDF default; this sets our timeout.
  esp_task_wdt_init(WDT_TIMEOUT_S, true);  // true: panic (and reboot) instead of just logging
  g_soft_lock = xSemaphoreCreateMutex();
  g_never_given = xSemaphoreCreateBinary();
  g_slot_control = g_soft.add("control", millis());
  g_slot_comms = g_soft.add("comms", millis());
  // Stalled task on core 1 at a priority below control, so control keeps running and the log
  // shows exactly one task going silent; the monitor lives on core 0 and cannot be starved.
  xTaskCreatePinnedToCore(control_task, "control", 2048, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(comms_task, "comms", 2048, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(monitor_task, "monitor", 3072, nullptr, 1, nullptr, 0);
}

void loop() { vTaskDelay(portMAX_DELAY); }
