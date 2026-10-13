// 03 Event groups for startup sequencing.
//
// Four bring-up tasks each set one bit when their subsystem is ready; the NTP task itself
// waits for the Wi-Fi bit first. setup() waits for all bits with a deadline and then uses
// startup_decide() (host-tested) to choose: run everything, run degraded (sensor up, cloud or
// storage missing), or halt (no sensor). Only then are the worker tasks created, so no task
// ever has to check "is Wi-Fi up yet?" in its loop.
//
// The queen-bee incubator does its bring-up sequentially inside setup() (fan relay first,
// then every module, then a self-test, then the tasks). An event group is what you reach for
// when bring-up steps run in parallel and some may fail.
#include <Arduino.h>

#include "Config.h"
#include "Startup.h"

static EventGroupHandle_t g_ready;

static void sensor_init_task(void*) {
  vTaskDelay(pdMS_TO_TICKS(SIM_SENSOR_MS));  // pretend: SHT31 self-test
  if (SENSOR_PRESENT) xEventGroupSetBits(g_ready, BIT_SENSOR);
  vTaskDelete(nullptr);
}

static void storage_init_task(void*) {
  vTaskDelay(pdMS_TO_TICKS(SIM_STORAGE_MS));  // pretend: mount SD, open NVS
  if (STORAGE_PRESENT) xEventGroupSetBits(g_ready, BIT_STORAGE);
  vTaskDelete(nullptr);
}

static void wifi_init_task(void*) {
  vTaskDelay(pdMS_TO_TICKS(SIM_WIFI_MS));  // pretend: WiFi.begin ... got IP
  xEventGroupSetBits(g_ready, BIT_WIFI);
  vTaskDelete(nullptr);
}

static void ntp_task(void*) {
  // Depends on Wi-Fi: wait for that one bit, without clearing it, for as long as it takes.
  xEventGroupWaitBits(g_ready, BIT_WIFI, pdFALSE, pdTRUE, portMAX_DELAY);
  vTaskDelay(pdMS_TO_TICKS(SIM_NTP_MS));  // pretend: SNTP sync
  xEventGroupSetBits(g_ready, BIT_TIME);
  vTaskDelete(nullptr);
}

static void control_task(void*) {
  for (;;) {
    Serial.println("[control] tick (needs only the sensor)");
    vTaskDelay(pdMS_TO_TICKS(REPORT_PERIOD_MS));
  }
}

static void uploader_task(void*) {
  for (;;) {
    Serial.println("[uploader] tick (needs wifi+time)");
    vTaskDelay(pdMS_TO_TICKS(REPORT_PERIOD_MS));
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  g_ready = xEventGroupCreate();
  uint32_t t0 = millis();
  xTaskCreate(sensor_init_task, "sensor_init", 2048, nullptr, 2, nullptr);
  xTaskCreate(storage_init_task, "storage_init", 2048, nullptr, 2, nullptr);
  xTaskCreate(wifi_init_task, "wifi_init", 2048, nullptr, 2, nullptr);
  xTaskCreate(ntp_task, "ntp", 2048, nullptr, 2, nullptr);

  // Wait for all bits in slices so progress can be printed; bits are not cleared on exit.
  StartupDecision d;
  uint32_t bits;
  char missing[48];
  do {
    bits = xEventGroupWaitBits(g_ready, BITS_ALL, pdFALSE, pdTRUE, pdMS_TO_TICKS(250));
    uint32_t elapsed = millis() - t0;
    d = startup_decide(bits, elapsed, STARTUP_DEADLINE_MS);
    startup_missing(bits, missing, sizeof missing);
    Serial.printf("[startup] %5lu ms  bits=0x%02lx  missing: %s\n", (unsigned long)elapsed,
                  (unsigned long)bits, missing[0] ? missing : "-");
  } while (d == StartupDecision::Wait);

  switch (d) {
    case StartupDecision::Run:
      Serial.println("[startup] all ready: starting control + uploader");
      xTaskCreate(control_task, "control", 2048, nullptr, 3, nullptr);
      xTaskCreate(uploader_task, "uploader", 2048, nullptr, 1, nullptr);
      break;
    case StartupDecision::RunDegraded:
      Serial.printf("[startup] deadline passed without %s: control only, no uploads\n", missing);
      xTaskCreate(control_task, "control", 2048, nullptr, 3, nullptr);
      break;
    case StartupDecision::Halt:
      Serial.println("[startup] no sensor: nothing safe to control, restarting in 10 s");
      vTaskDelay(pdMS_TO_TICKS(10000));
      ESP.restart();
      break;
    case StartupDecision::Wait:
      break;  // unreachable: the loop above exits only on a decision
  }
}

void loop() { vTaskDelay(portMAX_DELAY); }
