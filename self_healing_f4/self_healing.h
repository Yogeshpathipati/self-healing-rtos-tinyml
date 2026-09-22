#ifndef SELF_HEALING_H
#define SELF_HEALING_H

#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void execute_healing_protocol(float        anomaly_score,
                              float        cpu_load,
                              uint32_t     stack_free,
                              int32_t      isr_latency,
                              float        temperature,
                              TaskHandle_t worker_task_handle);

#ifdef __cplusplus
}
#endif

#endif
