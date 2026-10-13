// Host unit tests for lib/patterns_core (no hardware, no FreeRTOS).
//   make check              (plain g++)
//   pio test -e native      (PlatformIO native env, custom runner in test/)
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "Hysteresis.h"
#include "Periodic.h"
#include "SoftWatchdog.h"
#include "Startup.h"
#include "Stats.h"
#include "minitest.h"

// ---------- RunningStats / SeqGaps (sketches 01 and 06) ----------

static void stats_basic() {
  RunningStats s(100);
  CHECK_EQ(s.count, 0u);
  CHECK_EQ(s.mean(), 0.0);
  s.add(50);
  s.add(150);
  s.add(100);  // equal to threshold is not "over"
  CHECK_EQ(s.count, 3u);
  CHECK_EQ(s.min, 50);
  CHECK_EQ(s.max, 150);
  CHECK_EQ(s.over, 1u);
  CHECK(fabs(s.mean() - 100.0) < 1e-9);
  s.reset();
  CHECK_EQ(s.count, 0u);
  CHECK_EQ(s.over, 0u);
  CHECK_EQ(s.min, INT64_MAX);
}

static void stats_negative_values() {
  RunningStats s;
  s.add(-5);
  s.add(-20);
  CHECK_EQ(s.min, -20);
  CHECK_EQ(s.max, -5);
  CHECK_EQ(s.over, 0u);  // default threshold is INT64_MAX
}

static void seq_gaps_counts_lost_items() {
  SeqGaps g;
  g.seen(0);
  g.seen(1);
  g.seen(2);
  CHECK_EQ(g.gaps, 0u);
  g.seen(6);  // 3,4,5 lost
  CHECK_EQ(g.gaps, 1u);
  CHECK_EQ(g.missing, 3u);
  g.seen(7);
  g.seen(9);  // 8 lost
  CHECK_EQ(g.gaps, 2u);
  CHECK_EQ(g.missing, 4u);
  CHECK_EQ(g.received, 6u);
}

static void seq_gaps_restart_is_not_a_loss() {
  SeqGaps g;
  g.seen(40);
  g.seen(41);
  g.seen(0);  // producer rebooted
  CHECK_EQ(g.gaps, 1u);
  CHECK_EQ(g.missing, 0u);
  g.seen(1);
  CHECK_EQ(g.gaps, 1u);
}

// ---------- Hysteresis (sketch 02) ----------

static void hysteresis_switches_at_band_edges() {
  Hysteresis h(0.5f, 0.0f, 60.0f);
  CHECK_EQ(h.update(34.0f, 35.0f), true);   // below setpoint - band
  CHECK_EQ(h.update(34.7f, 35.0f), true);   // inside band: unchanged
  CHECK_EQ(h.update(34.99f, 35.0f), true);  // still inside
  CHECK_EQ(h.update(35.0f, 35.0f), false);  // reached setpoint
  CHECK_EQ(h.update(34.7f, 35.0f), false);  // inside band: stays off
  CHECK_EQ(h.update(34.4f, 35.0f), true);   // dropped below band again
}

static void hysteresis_never_heats_on_bad_reading() {
  Hysteresis h(0.5f, 0.0f, 60.0f);
  CHECK_EQ(h.update(30.0f, 35.0f), true);
  CHECK_EQ(h.update(NAN, 35.0f), false);
  CHECK_EQ(h.update(-10.0f, 35.0f), false);  // below the plausible range
  CHECK_EQ(h.update(85.0f, 35.0f), false);   // above it
  CHECK_EQ(h.update(30.0f, 35.0f), true);    // recovers once readings are sane
}

static void hysteresis_setpoint_change_takes_effect_immediately() {
  Hysteresis h(0.5f, 0.0f, 60.0f);
  CHECK_EQ(h.update(34.0f, 35.0f), true);
  CHECK_EQ(h.update(34.0f, 33.0f), false);  // setpoint lowered: now above it
}

// ---------- Startup decision (sketch 03) ----------

static void startup_all_bits_runs_before_deadline() {
  CHECK(startup_decide(BITS_ALL, 100, 5000) == StartupDecision::Run);
  CHECK(startup_decide(BITS_ALL | 0xF0, 100, 5000) == StartupDecision::Run);  // extra bits ignored
}

static void startup_waits_until_deadline() {
  CHECK(startup_decide(BIT_SENSOR | BIT_WIFI, 4999, 5000) == StartupDecision::Wait);
  CHECK(startup_decide(0, 0, 5000) == StartupDecision::Wait);
}

static void startup_degraded_without_cloud_or_storage() {
  CHECK(startup_decide(BIT_SENSOR, 5000, 5000) == StartupDecision::RunDegraded);
  CHECK(startup_decide(BIT_SENSOR | BIT_WIFI, 9000, 5000) == StartupDecision::RunDegraded);
  CHECK(startup_decide(BIT_SENSOR | BIT_STORAGE | BIT_WIFI, 9000, 5000) == StartupDecision::RunDegraded);
}

static void startup_halts_without_sensor() {
  CHECK(startup_decide(BIT_STORAGE | BIT_WIFI | BIT_TIME, 5000, 5000) == StartupDecision::Halt);
  CHECK(startup_decide(0, 60000, 5000) == StartupDecision::Halt);
}

static void startup_missing_names() {
  char buf[64];
  CHECK_EQ(startup_missing(BITS_ALL, buf, sizeof buf), 0);
  CHECK_STR_EQ(buf, "");
  CHECK_EQ(startup_missing(BIT_SENSOR | BIT_STORAGE, buf, sizeof buf), 2);
  CHECK_STR_EQ(buf, "wifi,time");
  CHECK_EQ(startup_missing(0, buf, sizeof buf), 4);
  CHECK_STR_EQ(buf, "sensor,storage,wifi,time");
  char tiny[8];  // truncates safely, still counts
  CHECK_EQ(startup_missing(0, tiny, sizeof tiny), 4);
  CHECK_STR_EQ(tiny, "sensor,");
}

// ---------- Periodic (sketch 04) ----------

static void periodic_does_not_drift() {
  Periodic p(100, 1000);
  uint32_t now = 1000;
  for (int i = 1; i <= 1000; i++) {
    now += 30 + (i % 7);  // work with jitter, always shorter than the period
    uint32_t sleep = p.schedule(now);
    now += sleep;         // the task wakes exactly at the deadline
    CHECK_EQ(now, 1000u + 100u * i);
  }
  CHECK_EQ(p.missed, 0u);
  CHECK_EQ(p.overruns, 0u);
}

static void periodic_counts_missed_deadlines() {
  Periodic p(100, 0);               // deadlines at 100, 200, 300, ...
  CHECK_EQ(p.schedule(250), 0u);    // finished 1.5 periods late: 100 went by, 200 is served now
  CHECK_EQ(p.overruns, 1u);
  CHECK_EQ(p.missed, 1u);
  CHECK_EQ(p.next, 300u);
  CHECK_EQ(p.schedule(300), 0u);    // exactly on the deadline: on time, nothing missed
  CHECK_EQ(p.overruns, 1u);
  CHECK_EQ(p.next, 400u);
  CHECK_EQ(p.schedule(310), 90u);   // back in phase with the original grid
  CHECK_EQ(p.schedule(850), 0u);    // 500, 600, 700 skipped; 800 served late
  CHECK_EQ(p.overruns, 2u);
  CHECK_EQ(p.missed, 4u);
  CHECK_EQ(p.next, 900u);
}

static void periodic_slightly_late_runs_now_without_skipping() {
  Periodic p(100, 0);
  CHECK_EQ(p.schedule(99), 1u);
  CHECK_EQ(p.next, 200u);
  CHECK_EQ(p.schedule(201), 0u);    // 1 ms late: run now, no deadline skipped
  CHECK_EQ(p.overruns, 1u);
  CHECK_EQ(p.missed, 0u);
  CHECK_EQ(p.next, 300u);
}

static void periodic_survives_millis_wraparound() {
  uint32_t start = 0xFFFFFFF0u;  // 16 ms before millis() wraps to 0
  Periodic p(100, start);
  CHECK_EQ(p.next, 84u);                   // wrapped deadline
  CHECK_EQ(p.schedule(0xFFFFFFFFu), 85u);  // still on time across the wrap
  CHECK_EQ(p.next, 184u);
  CHECK_EQ(p.schedule(200), 0u);           // 16 ms late
  CHECK_EQ(p.overruns, 1u);
  CHECK_EQ(p.missed, 0u);
  CHECK_EQ(p.next, 284u);
}

// ---------- SoftWatchdog (sketch 05) ----------

static void watchdog_reports_first_silent_task() {
  SoftWatchdog wdt(5000);
  int a = wdt.add("control", 0);
  int b = wdt.add("comms", 0);
  CHECK(a >= 0 && b >= 0 && a != b);
  CHECK_EQ(wdt.subscribed(), 2);
  CHECK(wdt.check(4000) == nullptr);
  CHECK(wdt.kick(a, 4000));
  uint32_t silent = 0;
  const char* who = wdt.check(5001, &silent);
  CHECK(who != nullptr && strcmp(who, "comms") == 0);
  CHECK_EQ(silent, 5001u);
  CHECK(wdt.kick(b, 5001));
  CHECK(wdt.check(9000) == nullptr);
  CHECK(wdt.check(9001) != nullptr);  // control last kicked at 4000
}

static void watchdog_remove_stops_monitoring() {
  SoftWatchdog wdt(1000);
  int a = wdt.add("stalled", 0);
  CHECK(wdt.remove(a));
  CHECK(wdt.check(100000) == nullptr);
  CHECK_EQ(wdt.subscribed(), 0);
  CHECK(!wdt.remove(a));          // already gone
  CHECK(!wdt.kick(a, 1));
  CHECK(!wdt.kick(-1, 1));
  CHECK(!wdt.kick(SoftWatchdog::kMaxTasks, 1));
}

static void watchdog_is_bounded() {
  SoftWatchdog wdt(1000);
  for (int i = 0; i < SoftWatchdog::kMaxTasks; i++) CHECK(wdt.add("t", 0) >= 0);
  CHECK_EQ(wdt.add("one too many", 0), -1);
  CHECK_EQ(wdt.subscribed(), SoftWatchdog::kMaxTasks);
}

int main() {
  RUN(stats_basic);
  RUN(stats_negative_values);
  RUN(seq_gaps_counts_lost_items);
  RUN(seq_gaps_restart_is_not_a_loss);
  RUN(hysteresis_switches_at_band_edges);
  RUN(hysteresis_never_heats_on_bad_reading);
  RUN(hysteresis_setpoint_change_takes_effect_immediately);
  RUN(startup_all_bits_runs_before_deadline);
  RUN(startup_waits_until_deadline);
  RUN(startup_degraded_without_cloud_or_storage);
  RUN(startup_halts_without_sensor);
  RUN(startup_missing_names);
  RUN(periodic_does_not_drift);
  RUN(periodic_counts_missed_deadlines);
  RUN(periodic_slightly_late_runs_now_without_skipping);
  RUN(periodic_survives_millis_wraparound);
  RUN(watchdog_reports_first_silent_task);
  RUN(watchdog_remove_stops_monitoring);
  RUN(watchdog_is_bounded);
  return mt_summary();
}
