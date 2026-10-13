// 01 Producer/consumer queue.
//
// A sampler task produces one Reading every Q_PRODUCE_PERIOD_MS and must never block. A
// consumer task takes readings out and, every Q_FLUSH_EVERY items, is busy for
// Q_FLUSH_BLOCK_MS (think: writing a block to the SD card or the cloud). The queue is the
// buffer between the two rates. When it is too short the producer drops readings and
// counts them; the consumer notices the holes from the sequence numbers.
//
// The queen-bee incubator does the same job with a 60-slot RingBuffer that the Datalog task
// drains once a minute; a FreeRTOS queue is the built-in equivalent for one producer and
// one consumer.
#include <Arduino.h>

#include "Config.h"
#include "Stats.h"

struct Reading {
  uint32_t seq;
  uint32_t ts_ms;
  float temp_c;
};

static QueueHandle_t g_queue;
static uint32_t g_dropped = 0;  // written by the producer only, read by the consumer

static void producer_task(void*) {
  uint32_t seq = 0;
  TickType_t last_wake = xTaskGetTickCount();
  for (;;) {
    Reading r{seq++, (uint32_t)millis(), 34.5f + 0.5f * sinf(millis() / 5000.0f)};  // fake SHT31
    // Timeout 0: a sampler must not wait on a slow consumer. Drop, count, carry on.
    if (xQueueSend(g_queue, &r, 0) != pdTRUE) g_dropped++;
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(Q_PRODUCE_PERIOD_MS));
  }
}

static void consumer_task(void*) {
  Reading r;
  SeqGaps gaps;
  RunningStats depth;  // how full the queue was when we took an item
  uint32_t taken = 0;
  for (;;) {
    if (xQueueReceive(g_queue, &r, portMAX_DELAY) != pdTRUE) continue;  // blocks until data
    gaps.seen(r.seq);
    depth.add(uxQueueMessagesWaiting(g_queue));
    if (++taken % Q_FLUSH_EVERY == 0) {
      vTaskDelay(pdMS_TO_TICKS(Q_FLUSH_BLOCK_MS));  // the slow part: the queue fills meanwhile
      Serial.printf("[consumer] received=%lu lost=%lu (in %lu gaps)  producer dropped=%lu  "
                    "queue depth max=%lld mean=%.1f of %d\n",
                    (unsigned long)gaps.received, (unsigned long)gaps.missing,
                    (unsigned long)gaps.gaps, (unsigned long)g_dropped, (long long)depth.max,
                    depth.mean(), Q_LENGTH);
    }
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  g_queue = xQueueCreate(Q_LENGTH, sizeof(Reading));  // copies each Reading in; no pointers
  xTaskCreatePinnedToCore(producer_task, "producer", 2048, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(consumer_task, "consumer", 3072, nullptr, 2, nullptr, 1);
  Serial.printf("01_queue: length %d, produce every %d ms, flush %d items for %d ms\n", Q_LENGTH,
                Q_PRODUCE_PERIOD_MS, Q_FLUSH_EVERY, Q_FLUSH_BLOCK_MS);
}

void loop() { vTaskDelay(portMAX_DELAY); }  // everything happens in the two tasks
