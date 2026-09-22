#include <STM32FreeRTOS.h>
#include "anomaly_detector.h"
#include "self_healing.h"

volatile bool g_forceHighCpu     = false;
volatile bool g_forceLowStack    = false;
volatile bool g_rebuildTask      = false;
volatile bool g_forceHighLatency = false;
volatile bool g_forceHighTemp    = false;

HardwareTimer *JitterTimer = NULL;

void JitterCallback()
{
  uint32_t delay_us = 10 + (micros() % 490); 
  delayMicroseconds(delay_us);
}

volatile uint32_t g_benchCount  = 0;
volatile uint32_t g_benchMax    = 0;
volatile bool     g_benchCalibrated = false;
volatile uint32_t g_cpuLoad = 0;

typedef enum {
  STATE_NORMAL = 0,
  STATE_WARNING,
  STATE_ERROR,
  STATE_SAFE
} SystemState;

volatile SystemState g_state = STATE_NORMAL;
static   uint32_t   g_errorSeconds = 0;

TaskHandle_t defaultTaskHandle      = NULL;
TaskHandle_t healthSupervisorHandle = NULL;

void BenchmarkTask(void* pvParameters)
{
  for(;;)
  {
    g_benchCount++;  
  }
}

void simulate_heavy_workload(int depth)
{
  if(depth <= 0) return;

  volatile float temp_buffer[10];
  for(int i = 0; i < 10; i++) {
    temp_buffer[i] = (depth * 3.14f) + i;
  }

  for(volatile int j = 0; j < 100; j++) { }

  simulate_heavy_workload(depth - 1);

  volatile float sum = 0;
  for(int i = 0; i < 10; i++) {
    sum += temp_buffer[i];
  }
}

void stack_overflow_attack(int depth)
{
  volatile uint8_t bloat[128];
  for(int i = 0; i < 128; i++) bloat[i] = (uint8_t)(depth ^ i);

  UBaseType_t remaining = uxTaskGetStackHighWaterMark(NULL);
  if(depth > 0 && remaining > 50) { 
    stack_overflow_attack(depth - 1);
  } else {
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  volatile uint8_t sink = bloat[64];
  (void)sink;
}

void StartDefaultTask(void* pvParameters)
{
  static uint32_t ramp_target = 0;

  vTaskDelay(pdMS_TO_TICKS(6000));

  for(;;)
  {
    if(g_forceHighCpu)
    {
      if(ramp_target < 50) 
        ramp_target = 50;
      else 
        ramp_target += 2;

      if(ramp_target > 98) 
        ramp_target = 98;

      uint32_t work_ms  = ramp_target;
      uint32_t sleep_ms = 100 - ramp_target;
      if(sleep_ms < 1) sleep_ms = 1;

      uint32_t deadline = millis() + work_ms;

      while((int32_t)(deadline - millis()) > 0)
      {
        int depth = ramp_target / 6;
        if(depth < 1) depth = 1;
        if(depth > 8) depth = 8;
        simulate_heavy_workload(depth);
      }

      vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    }
    else if(g_forceLowStack)
    {
      ramp_target = 0;
      
      volatile uint8_t stack_crusher[550];
      for(int i = 0; i < 550; i++) {
        stack_crusher[i] = (uint8_t)(i % 256);
      }
      
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    else
    {
      ramp_target = 0;

      float wave = sin(millis() / 3000.0f); 
      uint32_t target_cpu_load = 10 + (uint32_t)(((wave + 1.0f) / 2.0f) * 55.0f);
      
      target_cpu_load += random(-1, 2);
      if(target_cpu_load < 5)  target_cpu_load = 5;
      if(target_cpu_load > 85) target_cpu_load = 85;

      uint32_t work_ms  = target_cpu_load;
      uint32_t sleep_ms = 100 - work_ms;

      uint32_t deadline = millis() + work_ms;

      while((int32_t)(deadline - millis()) > 0)
      {
        int depth_target = target_cpu_load / 5; 
        if(depth_target < 1) depth_target = 1;
        if(depth_target > 8) depth_target = 8; 
        
        simulate_heavy_workload(depth_target);
      }

      vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    }
  }
}

float readChipTemp()
{
  __HAL_RCC_ADC1_CLK_ENABLE();

  ADC1->CR1  = 0;
  ADC1->CR2  = 0;

  ADC1->CR2 = (1UL << 23)   
            | (1UL << 20)   
            | (7UL << 17)  
            | (1UL << 0); 

  delayMicroseconds(15); 

  ADC1->SQR1  = 0;
  ADC1->SQR3  = 16;
  ADC1->SMPR1 = (7UL << 18);

  ADC1->SR    = 0;
  ADC1->CR2  |= (1UL << 22);

  uint32_t timeout = 100000UL;
  while(!(ADC1->SR & (1UL << 1)) && --timeout);

  if(timeout == 0) return 30.0f;

  uint16_t raw = (uint16_t)(ADC1->DR & 0x0FFF);

  float vsense_mV = (raw / 4095.0f) * 3300.0f;
  return ((1430.0f - vsense_mV) / 4.3f) + 25.0f;
}

void StartHealthSupervisor(void* pvParameters)
{
  static uint8_t calibCount = 0;
  static bool    calibDone  = false;

  for(;;)
  {
    g_benchCount     = 0;
    uint32_t t_start = millis();

    vTaskDelay(pdMS_TO_TICKS(1000));

    uint32_t bench   = g_benchCount;
    uint32_t elapsed = millis() - t_start;

    if(!calibDone)
    {
      if(bench > g_benchMax) g_benchMax = bench;
      calibCount++;
      if(calibCount >= 5) {
        calibDone = true;
        Serial.printf("[CAL] benchMax = %lu\r\n", (unsigned long)g_benchMax);
      }
    }

    uint32_t cpuLoad = 0;
    if(g_benchMax > 0)
      cpuLoad = (bench < g_benchMax) ? (100UL - (100UL * bench / g_benchMax)) : 0UL;
    g_cpuLoad = cpuLoad;

    uint32_t baseStack = (uint32_t)uxTaskGetStackHighWaterMark(defaultTaskHandle)
                         * sizeof(StackType_t);

    uint32_t hwBytes;
    if(g_forceLowStack) {
      hwBytes = baseStack;
    } else {
      int32_t simStack = 800 - ((int32_t)cpuLoad * 10);
      if(simStack < 0) simStack = 0;
      hwBytes = (uint32_t)simStack;
    }

    int32_t jitterMs     = (int32_t)elapsed - 1000;
    int32_t jitterCycles = jitterMs * 72;
    
    if(g_forceHighLatency) jitterCycles = 15000L;

    float temperature = readChipTemp();
    if(g_forceHighTemp) temperature = 85.0f;

    float features[4] = {
      (float)g_cpuLoad,
      (float)hwBytes,
      (float)jitterCycles,
      temperature
    };
    float anomaly_score = calculate_anomaly_score(features);

    SystemState next = STATE_NORMAL;
    if(anomaly_score >= 0.85f)
      next = STATE_ERROR;
    else if(anomaly_score >= 0.5f)
      next = STATE_WARNING;

    g_state = next;
    if(g_state == STATE_ERROR) g_errorSeconds++;
    else                       g_errorSeconds = 0;
    
    if(g_state == STATE_NORMAL && eTaskGetState(defaultTaskHandle) == eSuspended) {
       vTaskResume(defaultTaskHandle);
       Serial.printf("[SYS] System Cooled. Worker Task Auto-Resumed!\r\n");
    }

    if(g_errorSeconds >= 5)    g_state = STATE_SAFE;

    char tempStr[8], scoreStr[8];
    dtostrf(temperature,   4, 1, tempStr);
    dtostrf(anomaly_score, 4, 2, scoreStr);

    Serial.printf("STATE=%d CPU=%lu%% stackB=%lu jitter=%ldc temp=%sC ML_Score=%s\r\n",
                  (int)g_state,
                  (unsigned long)g_cpuLoad,
                  (unsigned long)hwBytes,
                  (long)jitterCycles,
                  tempStr,
                  scoreStr);

    static uint32_t bootTick      = 0;
    static bool     healingEnabled = false;
    if(!healingEnabled) {
      bootTick++;
      if(bootTick >= 10) {
        healingEnabled = true;
        Serial.println("[SYS] Healing engine ACTIVE.");
      } else {
        Serial.printf("[SYS] Healing armed in %lu sec...\r\n", 10 - bootTick);
      }
    }

    if(healingEnabled) {
      execute_healing_protocol(
        anomaly_score,
        (float)g_cpuLoad,
        (uint32_t)hwBytes,
        jitterCycles,
        temperature,
        defaultTaskHandle
      );
    }

    if (g_rebuildTask) {
      g_rebuildTask = false;
      
      if (defaultTaskHandle != NULL) {
        vTaskDelete(defaultTaskHandle);
        defaultTaskHandle = NULL;
      }
      
      vTaskDelay(pdMS_TO_TICKS(50));
      
      xTaskCreate(StartDefaultTask, "DefaultTask", 256, NULL, 1, &defaultTaskHandle);
      
      Serial.println("[SYS] Task memory flushed and rebuilt successfully. Resuming operations...");
    }
  }
}

void StartCommandTask(void* pvParameters)
{
  for(;;)
  {
    if(Serial.available())
    {
      char cmd = (char)Serial.read();
      switch(cmd)
      {
        case 'c':
          g_forceHighCpu = !g_forceHighCpu;
          Serial.printf("[CMD] g_forceHighCpu = %d\r\n", g_forceHighCpu);
          break;
        case 'o':
          g_forceLowStack = !g_forceLowStack;
          Serial.printf("[CMD] g_forceLowStack = %d\r\n", g_forceLowStack);
          break;
        case 't':
          g_forceHighTemp = !g_forceHighTemp;
          Serial.printf("[CMD] g_forceHighTemp = %d\r\n", g_forceHighTemp);
          break;
        case 'l':
          g_forceHighLatency = !g_forceHighLatency;
          Serial.printf("[CMD] g_forceHighLatency = %d\r\n", g_forceHighLatency);
          break;
        case 'r':
          Serial.printf("[STATUS] STATE=%d CPU=%lu%% Flags: cpu=%d tmp=%d lat=%d stk=%d\r\n",
                        (int)g_state, (unsigned long)g_cpuLoad,
                        g_forceHighCpu, g_forceHighTemp,
                        g_forceHighLatency, g_forceLowStack);
          break;
        default:
          break;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println("  Self-Healing STM32 — TinyML Edition");
  Serial.println("  c=CPU  t=Temp  l=Latency  s=Stack  r=Status");

  JitterTimer = new HardwareTimer(TIM2);
  JitterTimer->setOverflow(3500, MICROSEC_FORMAT); 
  JitterTimer->attachInterrupt(JitterCallback);
  JitterTimer->resume();

  xTaskCreate(BenchmarkTask,         "Bench",       128, NULL, 0, NULL);

  xTaskCreate(StartDefaultTask,      "DefaultTask", 256, NULL, 1, &defaultTaskHandle);
  xTaskCreate(StartCommandTask,      "CmdTask",     256, NULL, 1, NULL);

  xTaskCreate(StartHealthSupervisor, "HealthSuperv",512, NULL, 2, &healthSupervisorHandle);

  vTaskStartScheduler();
}

void loop() { }
