#pragma once

#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Web handlers run in the AsyncTCP task (priority 10), which preempts the
// Arduino loop task (priority 1) mid-step. Every touch of shared Workflow /
// StepperAgitator state is serialized through this mutex. Never hold it while
// calling into the TCP stack, so the lock order stays one-way.
class StateLock {
 public:
  StateLock() {
    xSemaphoreTakeRecursive(mutex(), portMAX_DELAY);
  }

  ~StateLock() {
    xSemaphoreGiveRecursive(mutex());
  }

  StateLock(const StateLock &) = delete;
  StateLock &operator=(const StateLock &) = delete;

 private:
  static SemaphoreHandle_t mutex() {
    static SemaphoreHandle_t handle = xSemaphoreCreateRecursiveMutex();
    return handle;
  }
};
