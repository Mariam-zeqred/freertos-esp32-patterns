// 02 Mutex-guarded shared state.
//
// Four tasks share one SharedState struct: a sensor writes the temperature, a "user" flips the
// setpoint, a control loop reads both and writes the heater output, a display reads it all.
// Every access goes through snapshot()/modify(), which take one mutex with a timeout, so a
// reader always sees a consistent struct and a writer that cannot get the lock in
// LOCK_TIMEOUT_MS is counted instead of silently blocking forever.
//
// The queen-bee incubator's SharedState module is this pattern with more fields; its
// Interlock decides from that state whether the heater and fan may run, which is why a
// torn or stale read there matters.
//
// LOCKING 0 removes the mutex: `seq` and `seq_copy` are written together by the sensor,
// and the display counts how often it saw them differ (a torn read).
#include <Arduino.h>

#include "Config.h"
#include "Hysteresis.h"

struct SharedState {
  float temp_c = NAN;
  float setpoint_c = SETPOINT_A_C;
  bool heater_on = false;
  uint32_t seq = 0;       // sensor write counter ...
  uint32_t seq_copy = 0;  // ... written together with seq; must always be equal
};

static SharedState g_state;
static SemaphoreHandle_t g_lock;
static uint32_t g_lock_timeouts = 0;

// Run `f(g_state)` while holding the lock. Returns false if the lock was not obtained.
template <class F>
static bool modify(F f) {
#if LOCKING
  if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) != pdTRUE) {
    g_lock_timeouts++;
    return false;
  }
#endif
  f(g_state);
#if LOCKING
  xSemaphoreGive(g_lock);
#endif
  return true;
}

static bool snapshot(SharedState& out) {
  return modify([&](SharedState& s) { out = s; });  // copy out, then work on the copy unlocked
}

static void sensor_task(void*) {
  TickType_t last = xTaskGetTickCount();
  uint32_t n = 0;
  for (;;) {
    float t = 34.6f + 0.6f * sinf(millis() / 20000.0f);  // slow fake drift
    n++;
    modify([&](SharedState& s) {
      s.temp_c = t;
      s.seq = n;
      s.seq_copy = n;
    });
    vTaskDelayUntil(&last, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
  }
}

static void user_task(void*) {
  bool a = true;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(SETPOINT_TOGGLE_MS));
    a = !a;
    modify([&](SharedState& s) { s.setpoint_c = a ? SETPOINT_A_C : SETPOINT_B_C; });
  }
}

static void control_task(void*) {
  Hysteresis h(HYST_BAND_C, 0.0f, 60.0f);
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    SharedState s;
    if (snapshot(s)) {
      bool on = h.update(s.temp_c, s.setpoint_c);  // decided on the copy, outside the lock
      modify([&](SharedState& st) { st.heater_on = on; });
    }
    vTaskDelayUntil(&last, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
  }
}

static void display_task(void*) {
  uint32_t torn = 0, reads = 0, since_report = 0;
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    SharedState s;
    if (snapshot(s)) {
      reads++;
      if (s.seq != s.seq_copy) torn++;
    }
    since_report += DISPLAY_PERIOD_MS;
    if (since_report >= REPORT_PERIOD_MS) {
      since_report = 0;
      Serial.printf("[display] temp=%.2f set=%.1f heater=%s  reads=%lu torn=%lu lock_timeouts=%lu\n",
                    s.temp_c, s.setpoint_c, s.heater_on ? "ON" : "off", (unsigned long)reads,
                    (unsigned long)torn, (unsigned long)g_lock_timeouts);
    }
    vTaskDelayUntil(&last, pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  g_lock = xSemaphoreCreateMutex();
  // Writer and reader on different cores so a missing lock can really interleave.
  xTaskCreatePinnedToCore(sensor_task, "sensor", 2048, nullptr, 3, nullptr, 0);
  xTaskCreatePinnedToCore(user_task, "user", 2048, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(control_task, "control", 3072, nullptr, 4, nullptr, 1);
  xTaskCreatePinnedToCore(display_task, "display", 3072, nullptr, 1, nullptr, 1);
  Serial.printf("02_mutex: LOCKING=%d, lock timeout %d ms\n", LOCKING, LOCK_TIMEOUT_MS);
}

void loop() { vTaskDelay(portMAX_DELAY); }
