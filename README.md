# Self-Aware Embedded System: AI-Driven Self-Healing FreeRTOS Architecture on STM32


A resilient, meta-cognitive microcontroller platform implemented on **STM32 (ARM Cortex-M)** with **FreeRTOS**. Rather than relying on traditional, blunt watchdog timers (WDT) that force whole-system restarts upon fault detection, this architecture combines an **on-device TinyML Autoencoder** with a **deterministic Self-Healing Actuation Matrix**. 

The system achieves **autonomous, surgical fault resolution with a 50 ms task-recovery time**, preserving serial connectivity, task scheduling, and system uptime across critical runtime failures.

---

## Architecture Overview

```
                          ┌────────────────────────┐
                          │   Physical STM32 Core  │
                          └───────────┬────────────┘
                                      │
               ┌──────────────────────┴──────────────────────┐
               ▼                                             ▼
     [Target Worker Task]                          [HealthSupervisor Task]
  (Runs Dynamic Workloads)                       (Highest Priority, Period = 1s)
               │                                             │
               │ Generates Runtime Telemetry                 │ 1. Ingests 4D Vector
               └───────────────────────────────►             │    [CPU, Stack, Jitter, Temp]
                                                             │
                                                             ▼
                                                ┌───────────────────────────┐
                                                │  TinyML Autoencoder (C++) │
                                                │  Inference Latency: < 1 ms │
                                                │  Footprint: < 1 KB Flash  │
                                                └─────────────┬─────────────┘
                                                              │
                                            Reconstruction Loss (MSE) > 0.85?
                                            ┌─────────────────┴─────────────────┐
                                            │                                   │
                                            ▼ NO                                ▼ YES
                                    [Normal State]                   [Self-Healing Matrix]
                                   (Continue Cycle)             ┌─────────────────┴─────────────────┐
                                                                │                                   │
                                                       (Condition Check)                   (Actuation Action)
                                                                ├─► CPU/Temp Runaway   ──► Task Shedding (vTaskSuspend)
                                                                ├─► Stack Exhaustion   ──► Surgical Rebirth (50 ms)
                                                                ├─► Interrupt Flood    ──► NVIC IRQ Masking (< 1 ms)
                                                                └─► Deadlock/Unknown   ──► Limp Mode SOS Beacon
```

---

## Key Performance Benchmarks

| Metric | Measured Value | Description |
| :--- | :--- | :--- |
| **Task Rebirth Recovery Time** | **50 ms** | Quarantines leaky thread, flushes heap, respawns clean task clone |
| **Silicon Interrupt Masking** | **$< 1\text{ ms}$** ($\approx 2\text{--}5\ \mu\text{s}$) | Direct NVIC register reconfiguration disables misbehaving IRQ lines |
| **Dynamic Task Shedding** | **$< 1\text{ ms}$** ($\approx 10\ \mu\text{s}$) | Suspends runaway task to permit thermal and processor recovery |
| **Model Inference Latency** | **$< 1\text{ ms}$** | On-device 4 $\rightarrow$ 3 $\rightarrow$ 4 feedforward autoencoder (24 MACs) |
| **Model Memory Footprint** | **$< 1\text{ KB}$ Flash, 0 bytes Heap** | Zero dynamic allocation (`malloc`), pure stack and static buffers |
| **False Positive Rate** | **$< 2\%$** | Contextual scoring differentiates valid high CPU from abnormal leaks |

---

## Self-Healing Actuation Matrix

When the Autoencoder anomaly score exceeds the critical threshold (`Score > 0.85`), the supervisor bypasses system reboot and dispatches a surgical remediation:

1. **Condition A — Dynamic Task Shedding (`HEAL-A`)**:
   * **Trigger:** $\text{CPU} > 90\%$ or $\text{Temperature} > 70^\circ\text{C}$
   * **Mechanism:** FreeRTOS `vTaskSuspend()` immediately puts the worker task to sleep, dropping CPU utilization to $\approx 0\%$ and allowing the MCU to cool down. Once the temperature and CPU baseline normalize, the supervisor autonomously calls `vTaskResume()`.
2. **Condition B — Surgical Task Rebirth (`HEAL-B`)**:
   * **Trigger:** $\text{Stack Free} < 300\text{ bytes}$ (Stack exhaustion attack / memory leak)
   * **Mechanism:** Rather than allowing a HardFault, the supervisor invokes `vTaskDelete(targetTaskHandle)`, yields $50\text{ ms}$ (`vTaskDelay(pdMS_TO_TICKS(50))`) for the FreeRTOS Idle task to reclaim stack memory into the heap, and dynamically re-instantiates `xTaskCreate()` with a pristine stack.
3. **Condition D — Silicon Interrupt Masking (`HEAL-D`)**:
   * **Trigger:** $\text{ISR Jitter} > 10,000\text{ cycles}$ (Spurious interrupt flooding)
   * **Mechanism:** Directly reconfigures the ARM Cortex-M Nested Vectored Interrupt Controller via `NVIC_DisableIRQ()` on non-essential peripheral lines (`EXTI0-2`, `TIM3`), relieving scheduler starvation.
4. **Condition C — Limp Mode SOS Beacon (`HEAL-C`)**:
   * **Trigger:** Anomaly score $> 0.85$ without a matching single-variable trigger (Deadlock / Mutex starvation)
   * **Mechanism:** Failsafe catch-all. Blinks an SOS beacon on the status LED for 30 seconds to alert operators before issuing a clean `NVIC_SystemReset()`.

---

## Repository Structure

```
├── self_healing_f4/
│   ├── self_healing_f4.ino       # Main Arduino sketch & FreeRTOS task scheduling
│   ├── anomaly_detector.h        # C API header for the TinyML autoencoder
│   ├── anomaly_detector.cpp      # 4->3->4 inference engine (no STL, no malloc)
│   ├── self_healing.h            # Self-healing interface definitions
│   ├── self_healing.cpp          # Self-healing actuation logic (HEAL A/B/C/D)
│   └── dashboard.py              # Real-time Matplotlib & PySerial telemetry GUI
├── step1_parse_logs.py           # Offline log parser: raw UART logs -> clean CSV
├── step2_train_model.py          # MLPRegressor trainer & automated C array exporter
├── all_data.csv                  # Full recorded dataset across normal & anomaly states
├── normal_data.csv               # Pristine baseline dataset for autoencoder training
├── raw_serial_log.txt            # Sample telemetry stream captured from UART
├── weights_output.txt            # Autoencoder weights and normalization coefficients
└── README.md                     # Comprehensive technical documentation
```

---

## Getting Started

### 1. Hardware Requirements
* **Microcontroller:** STM32F103C8T6 (Blue Pill) or STM32F401/F411 Nucleo.
* **Serial Interface:** USB-to-UART converter (CP2102, FT232RL, or CH340).
* **Connections:**
  * STM32 `PA2` (TX) $\rightarrow$ USB-TTL `RX`
  * STM32 `PA3` (RX) $\rightarrow$ USB-TTL `TX`
  * `GND` $\rightarrow$ `GND`

### 2. Software Requirements
* **Arduino IDE** (with STM32duino Core and `STM32FreeRTOS` library installed).
* **Python 3.8+** with dependencies:
  ```powershell
  pip install pyserial matplotlib numpy scikit-learn
  ```

### 3. Flashing Firmware
1. Open [`self_healing_f4/self_healing_f4.ino`](self_healing_f4/self_healing_f4.ino) in Arduino IDE.
2. Under **Tools > Board**, select **Generic STM32F1 series** (or your respective STM32 board).
3. Set **Upload method** to **STLink** or **Serial**.
4. Compile and upload to the board.

### 4. Running Live Telemetry Dashboard
1. Verify the assigned COM port in Device Manager or Arduino IDE (e.g. `COM4`).
2. Update line 8 in [`self_healing_f4/dashboard.py`](self_healing_f4/dashboard.py):
   ```python
   SERIAL_PORT = 'COM4'
   ```
3. Close the Arduino Serial Monitor (only one application can hold the serial port).
4. Run the dashboard:
   ```powershell
   python self_healing_f4/dashboard.py
   ```

---

## Interactive Fault Injection

When running `dashboard.py`, you can test the system's resilience by pressing keys directly on the Matplotlib window:

| Key Press | Attack Injected | System Behavior & Self-Healing Action |
| :---: | :--- | :--- |
| `c` | **CPU Runaway Ramp** | Workload ramps up from 50% to 98%. When combined with thermal rise, **HEAL-A** suspends task. |
| `o` | **Stack Exhaustion Crash** | Memory allocated aggressively. At $<300\text{ bytes}$, **HEAL-B** triggers **50 ms task rebirth**. |
| `t` | **Thermal Spike Injection** | Internal temperature simulated at $85^\circ\text{C}$. Triggers thermal throttling. |
| `l` | **ISR Latency / Flooding** | Interrupt jitter spikes to $15,000\text{ cycles}$. **HEAL-D** masks non-essential IRQs. |

---

## Offline Model Training Pipeline

If you collect new hardware telemetry and wish to retrain the TinyML Autoencoder:

```powershell
# 1. Parse raw UART dump into baseline training CSV
python step1_parse_logs.py

# 2. Train the 4->3->4 autoencoder and generate C arrays
python step2_train_model.py
```

The script will automatically export tuned weights (`W_enc`, `b_enc`, `W_dec`, `b_dec`, `MSE_MAX`) to `weights_output.txt`. Copy these arrays into [`self_healing_f4/anomaly_detector.cpp`](self_healing_f4/anomaly_detector.cpp) and re-flash the MCU.

---

## License
Distributed under the MIT License. See `LICENSE` for more information.
