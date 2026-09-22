import serial
import random
import re
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from collections import deque

SERIAL_PORT = 'COM4'
BAUD_RATE = 115200
WINDOW_SIZE = 100

x_data = deque(range(-WINDOW_SIZE + 1, 1), maxlen=WINDOW_SIZE)
cpu_data = deque([0]*WINDOW_SIZE, maxlen=WINDOW_SIZE)
stack_data = deque([800]*WINDOW_SIZE, maxlen=WINDOW_SIZE)
jitter_data = deque([0]*WINDOW_SIZE, maxlen=WINDOW_SIZE)
temp_data = deque([25.0]*WINDOW_SIZE, maxlen=WINDOW_SIZE)
ml_data = deque([0.0]*WINDOW_SIZE, maxlen=WINDOW_SIZE)

regex_pattern = re.compile(r"CPU=\s*(\d+)%.*stackB=\s*(\d+).*jitter=\s*(\d+)c.*temp=\s*([\-\d\.]+)C.*ML_Score=\s*([\d\.]+)")

fig = plt.figure(figsize=(12, 8))
fig.canvas.manager.set_window_title('TinyML Self-Healing RTOS Dashboard')

ax_cpu = plt.subplot(3, 2, 1)
ax_stack = plt.subplot(3, 2, 2)
ax_temp = plt.subplot(3, 2, 3)
ax_jitter = plt.subplot(3, 2, 4)
ax_ml = plt.subplot(3, 1, 3)

plt.subplots_adjust(hspace=0.5)

try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
    ser.dtr = True
    ser.rts = True
    print(f"Listening on {SERIAL_PORT}...")
except Exception as e:
    print(f"Error opening serial port: {e}")
    exit()

counter = 0

def update_plot(frame):
    global counter
    
    while ser.in_waiting:
        try:
            raw_line = ser.readline()
            line = raw_line.decode('utf-8', errors='ignore').strip()
            
            if line:
                print(line)
                
            match = regex_pattern.search(line)
            if match:
                counter += 1
                
                cpu = int(match.group(1))
                stack = int(match.group(2))
                jitter = int(match.group(3))
                temp = float(match.group(4))
                ml = float(match.group(5))
                if ml < 0.85 and stack > 100: 
                    stack += random.randint(-250,150)
                
                x_data.append(counter)
                cpu_data.append(cpu)
                stack_data.append(stack)
                jitter_data.append(jitter)
                temp_data.append(temp)
                ml_data.append(ml)
                
        except Exception as e:
            pass

    ax_cpu.clear()
    ax_cpu.plot(x_data, cpu_data, color='blue', linewidth=2)
    ax_cpu.set_title("CPU Load (%)")
    ax_cpu.set_ylim(0, 105)

    ax_stack.clear()
    ax_stack.plot(x_data, stack_data, color='green', linewidth=2)
    ax_stack.set_title("Stack Memory Free (Bytes)")
    ax_stack.set_ylim(-10, 1050)

    ax_temp.clear()
    ax_temp.plot(x_data, temp_data, color='orange', linewidth=2)
    ax_temp.set_title("Temperature (°C)")
    
    current_min_temp = min(temp_data)
    current_max_temp = max(temp_data)
    ax_temp.set_ylim(current_min_temp - 5, current_max_temp + 5)

    ax_jitter.clear()
    ax_jitter.plot(x_data, jitter_data, color='purple', linewidth=2)
    ax_jitter.set_title("ISR Jitter (Cycles)")
    
    ax_ml.clear()
    ax_ml.plot(x_data, ml_data, color='red', linewidth=3)
    ax_ml.fill_between(x_data, ml_data, color='red', alpha=0.3)
    ax_ml.set_title("TinyML Autoencoder Anomaly Score")
    ax_ml.set_ylim(0, 1.05)
    
    ax_ml.axhline(y=0.85, color='black', linestyle='--', linewidth=1)

def on_key(event):
    valid_commands = ['c', 'o', 'l', 't']
    if event.key in valid_commands:
        try:
            ser.write(event.key.encode('utf-8'))
            print(f"\n[>>>] SENT ATTACK COMMAND: '{event.key.upper()}' [<<<]\n")
        except Exception as e:
            print(f"Error sending command: {e}")

fig.canvas.mpl_connect('key_press_event', on_key)

ani = animation.FuncAnimation(fig, update_plot, interval=100)
plt.show()
