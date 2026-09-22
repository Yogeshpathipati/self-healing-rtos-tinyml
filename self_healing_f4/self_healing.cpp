#include <stdio.h>
#include <stdlib.h>
#include <Arduino.h>
#include "self_healing.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "stm32f1xx.h"
#ifdef __cplusplus
}
#endif

extern volatile bool g_forceHighCpu;
extern volatile bool g_forceHighTemp;
extern volatile bool g_forceHighLatency;

#define THRESHOLD_ANOMALY_CRITICAL  0.85f
#define THRESHOLD_TEMP_HIGH         70.0f
#define THRESHOLD_CPU_HIGH          90.0f
#define THRESHOLD_STACK_LOW         300U
#define THRESHOLD_LATENCY_HIGH      10000L

#define ERROR_LED_PORT              GPIOA
#define ERROR_LED_PIN               GPIO_PIN_5

static const IRQn_Type NON_ESSENTIAL_IRQS[] = {
    EXTI0_IRQn,
    EXTI1_IRQn,
    EXTI2_IRQn,
    TIM3_IRQn,
};
#define NON_ESSENTIAL_IRQ_COUNT  (sizeof(NON_ESSENTIAL_IRQS) / sizeof(NON_ESSENTIAL_IRQS[0]))

static volatile uint32_t s_last_error_code = 0U;
static volatile int s_irqs_masked = 0;

static void healing_task_shedding(TaskHandle_t worker_task_handle,
                                  float cpu_load, float temperature)
{
    bool cpu_high  = (cpu_load > THRESHOLD_CPU_HIGH);
    bool temp_high = (temperature > THRESHOLD_TEMP_HIGH);

    char cpuStr[8], tmpStr[8];
    dtostrf(cpu_load,    3, 0, cpuStr);
    dtostrf(temperature, 4, 1, tmpStr);

    if(cpu_high && temp_high)
        Serial.printf("[HEAL-A] THERMAL + CPU RUNAWAY DETECTED (CPU=%s%% Temp=%sC)\r\n", cpuStr, tmpStr);
    else if(cpu_high)
        Serial.printf("[HEAL-A] CPU OVERLOAD DETECTED (%s%%)\r\n", cpuStr);
    else
        Serial.printf("[HEAL-A] THERMAL RUNAWAY DETECTED (%sC)\r\n", tmpStr);

    Serial.printf("[HEAL-A] Suspending worker task to recover...\r\n");

    if(worker_task_handle != NULL)
    {
        vTaskSuspend(worker_task_handle);
        Serial.printf("[HEAL-A] Worker task suspended. Awaiting recovery.\r\n");
        g_forceHighCpu = false;
        g_forceHighTemp = false;
        Serial.printf("[HEAL-A] -> Fault flags cleared by suspension.\r\n");
    }
    else
    {
        Serial.printf("[HEAL-A] WARNING: worker_task_handle is NULL. Cannot suspend.\r\n");
    }
}

extern volatile bool g_forceLowStack;
extern volatile bool g_rebuildTask;

static void healing_controlled_reboot(uint32_t stack_free)
{
    s_last_error_code = (0xDEADUL << 16U) | (stack_free & 0xFFFFU);

    Serial.println("[HEAL-B] STACK EXHAUSTION / MEMORY LEAK DETECTED");
    Serial.printf("[HEAL-B] stack_free=%lu bytes (limit=%u). Error=0x%08lX\r\n",
           (unsigned long)stack_free,
           THRESHOLD_STACK_LOW,
           (unsigned long)s_last_error_code);
           
    Serial.println("[HEAL-B] Quarantining corrupted memory and rebuilding task...");
    
    g_forceLowStack = 0; 
    g_rebuildTask = true; 
}

static void healing_interrupt_masking(int32_t isr_latency)
{
    Serial.printf("[HEAL-D] ISR LATENCY SPIKE DETECTED (jitter=%ld cycles, limit=%ld)\r\n",
           (long)isr_latency, (long)THRESHOLD_LATENCY_HIGH);

    if(s_irqs_masked)
    {
        Serial.printf("[HEAL-D] IRQs already masked. System still recovering...\r\n");
        return;
    }

    Serial.printf("[HEAL-D] Masking %u non-essential NVIC interrupt line(s)...\r\n",
           (unsigned)NON_ESSENTIAL_IRQ_COUNT);

    for(uint32_t i = 0; i < NON_ESSENTIAL_IRQ_COUNT; i++)
    {
        NVIC_DisableIRQ(NON_ESSENTIAL_IRQS[i]);
        Serial.printf("[HEAL-D]   Disabled IRQ %d\r\n", (int)NON_ESSENTIAL_IRQS[i]);
    }

    s_irqs_masked = 1;
    Serial.printf("[HEAL-D] Interrupt masking complete. RTOS scheduler relieved.\r\n");
    g_forceHighLatency = false;
    Serial.printf("[HEAL-D] -> Mock jitter cleared.\r\n");
    Serial.printf("[HEAL-D] To restore: reset the board or re-enable via NVIC_EnableIRQ().\r\n");
}

static void healing_limp_mode(float anomaly_score)
{
    char scoreStr[8];
    dtostrf(anomaly_score, 4, 2, scoreStr);

    Serial.printf("[HEAL-C] DEADLOCK / UNKNOWN CRITICAL ANOMALY (score=%s)\r\n", scoreStr);
    Serial.printf("[HEAL-C] No specific hardware fault found. Entering LIMP MODE.\r\n");
    Serial.printf("[HEAL-C] Possible cause: task deadlock or mutex starvation.\r\n");
    Serial.printf("[HEAL-C] SOS beacon for 30 seconds, then auto-reboot.\r\n");

    for(int i = 0; i < 30; i++)
    {
        HAL_GPIO_TogglePin(ERROR_LED_PORT, ERROR_LED_PIN);
        Serial.printf("[SOS] SYSTEM CRITICAL (%d/30) — AUTO-REBOOT IMMINENT\r\n", i + 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    Serial.printf("[SOS] Rebooting now...\r\n");
    delay(100);
    NVIC_SystemReset();
}

void execute_healing_protocol(float        anomaly_score,
                              float        cpu_load,
                              uint32_t     stack_free,
                              int32_t      isr_latency,
                              float        temperature,
                              TaskHandle_t worker_task_handle)
{
    if(anomaly_score <= THRESHOLD_ANOMALY_CRITICAL)
    {
        return;
    }

    char scoreStr[8], tempStr[8], cpuStr[8];
    dtostrf(anomaly_score, 4, 2, scoreStr);
    dtostrf(temperature,   4, 1, tempStr);
    dtostrf(cpu_load,      4, 0, cpuStr);

    Serial.printf("[HEAL] *** CRITICAL ANOMALY score=%s CPU=%s%% Temp=%sC "
           "Stack=%lu Jitter=%ld ***\r\n",
           scoreStr, cpuStr, tempStr,
           (unsigned long)stack_free, (long)isr_latency);

    if((temperature > THRESHOLD_TEMP_HIGH) || (cpu_load > THRESHOLD_CPU_HIGH))
    {
        healing_task_shedding(worker_task_handle, cpu_load, temperature);
        return;
    }

    if(isr_latency > THRESHOLD_LATENCY_HIGH)
    {
        healing_interrupt_masking(isr_latency);
        g_forceHighLatency = 0;
        return;
    }

    if(stack_free < THRESHOLD_STACK_LOW)
    {
        healing_controlled_reboot(stack_free);
        return; 
    }

    healing_limp_mode(anomaly_score);
}
